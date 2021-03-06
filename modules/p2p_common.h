#pragma once
#include "resource.h"
#include "sim.h"
#include "common.h"
#include "record.h"
#include "flow.h"

struct def_user {
	double bit_rate;
	enum node_state next_state;
	void *trigger;
	//low water mark: stop playing
	//high water mark: start playing
	int highwm, lowwm;
	int buffer_pos;
	id_t resource;
	double last_update, last_speed;
	struct node *n;
	struct event *e;
	struct resource_provider *p;
	struct list_head nodes;
	struct list_head time_zones[24];
	int time_zone;
	//0 = always online, 1 = not so
	int type;
	enum node_state nt;
	int cloud_push_dst;
};

struct server {
	struct node *n;
	struct list_head servers;
};

struct cloud_node {
	struct node *n;
	struct list_head cloud_nodes;
};

enum ue_type {
	DONE_PLAY,
	PAUSE_BUFFERING,
	DONE_BUFFERING,
	NEW_CONNECTION,
	NEW_RESOURCE,
	HOUR_PASS,
	SIM_END,
};

struct user_event {
	enum ue_type type;
	struct def_user *d;
	void *data;
};

struct nv_pair {
	struct node *n;
	int val;
};

struct def_sim {
	//Resource with hit prob
	//The prob is estimated using zipf distribution
	//See: http://home.ifi.uio.no/~griff/papers/62.pdf
	struct list_head rsrc_probs;
	//Servers are immutable
	struct list_head servers;
	struct list_head cloud_nodes;
	struct list_head nodes;
	struct list_head time_zones[24];
	struct skip_list_head rms;
	struct resource_entry *rsrcs;
	//resource number limit
	int max_rsrc, nrsrc;
	int nsvr, ncld, ncld_on, nnd;
	int start_hour;//Start hour in UTC+0
	struct nv_pair *eval_table;
	int eval_size;
	int (*fetch_metric)(struct node *, void *);
	int (*push_metric)(struct node *, void *);
	void (*new_conn_cb)(struct node *, id_t, struct sim_state *);
};

static inline struct node *
p2p_new_node(struct sim_state *s){
	struct node *n = sim_create_node(s);
	struct def_user *d;
	struct def_sim *ds = s->user_data;
	n->user_data = d = (struct def_user *)talloc(1, struct def_user);
	d->n = n;
	d->nt = N_IDLE;
	list_add(&d->nodes, &ds->nodes);

	ds->nnd++;
	if (ds->nnd > ds->eval_size) {
		ds->eval_size <<= 1;
		ds->eval_table = realloc(ds->eval_table,
					 ds->eval_size*sizeof(struct nv_pair));
	}
	return n;
}

static inline struct node *
p2p_new_server(struct sim_state *s){
	struct node *n = p2p_new_node(s);
	struct def_sim *ds = s->user_data;
	struct def_user *d = n->user_data;
	sim_node_change_state(n, N_SERVER, s);
	d->next_state = n->state;
	d->nt = N_SERVER;
	struct server *ss = talloc(1, struct server);
	ss->n = n;
	list_add(&ss->servers, &ds->servers);

	ds->nsvr++;
	return n;
}

static inline struct node *
p2p_new_cloud(struct sim_state *s){
	struct node *n = p2p_new_node(s);
	struct def_sim *ds = s->user_data;
	struct def_user *d = n->user_data;
	d->nt = N_CLOUD;
	n->state = N_OFFLINE;
	struct cloud_node *cn = talloc(1, struct cloud_node);
	cn->n = n;
	list_add(&cn->cloud_nodes, &ds->cloud_nodes);

	ds->ncld++;
	return n;
}

static inline void
assign_timezone(struct node *n, int time_zone, struct sim_state *s){
	struct def_user *d = n->user_data;
	struct def_sim *ds = s->user_data;
	int t = d->time_zone;
	if (d->time_zones[t].prev && d->time_zones[t].next)
		list_del(&d->time_zones[t]);
	d->time_zone = time_zone;
	list_add(&d->time_zones[time_zone], &ds->time_zones[time_zone]);
}

static inline void
init_sim_size(struct sim_state *s, int max, size_t size){
	assert(size > sizeof(struct def_sim));

	int i;
	struct def_sim *ds = (struct def_sim *)calloc(1, size);
	INIT_LIST_HEAD(&ds->servers);
	INIT_LIST_HEAD(&ds->cloud_nodes);
	INIT_LIST_HEAD(&ds->rsrc_probs);
	INIT_LIST_HEAD(&ds->nodes);
	for (i = 0; i < 24; i++)
		INIT_LIST_HEAD(&ds->time_zones[i]);

	skip_list_init_head(&ds->rms);

	ds->max_rsrc = max;
	s->user_data = ds;
	ds->eval_size = 1;
	ds->eval_table = talloc(1, struct nv_pair);
}

static inline void
init_sim(struct sim_state *s, int max){
	init_sim_size(s, max, sizeof(struct def_sim));
}

static inline int distance_metric(struct node *n, void *data){
	struct node *c = (struct node *)data;
	int ans;
	struct def_user *d1, *d2;
	d1 = n->user_data;
	d2 = c->user_data;
	ans = d1->time_zone-d2->time_zone;
	return ans < 0 ? -ans : ans;
}

static inline double distance_based_bw(void *_a, void *_b){
	struct def_user *a = _a, *b = _b;
	//Assmue linear decrease from 4000~1700
	int d = a->time_zone-b->time_zone;
	if (d < 0)
		d = -d;
	return 4000-100*d;
}

static inline int share_metric(struct node *n, void *data){
	struct node *c = (struct node *)data;
	double bw = distance_based_bw(n->user_data, c->user_data);
	double total = n->total_bwupbound[0]+bw;
	double max = n->maximum_bandwidth[0];
	if (total > max)
		return -bw*max/total;
	return -bw;
}

static inline double distance_type_based_bw(void *_a, void *_b){
	struct def_user *a = _a, *b = _b;
	//Assmue linear decrease from 4000~1700
	int d = a->time_zone-b->time_zone;
	if (d < 0)
		d = -d;
	if ((a->nt == N_SERVER || a->nt == N_CLOUD) &&
	    (b->nt == N_SERVER || b->nt == N_CLOUD))
		//cloud/server <-> cloud/server
		//different formula
		return 80000-3500*d;
	return 4000-100*d;
}

static inline double distance_based_delay(void *_a, void *_b){
	//Assmue linear increase from 0~460ms
	struct def_user *a = _a, *b = _b;
	int d = a->time_zone-b->time_zone;
	if (d < 0)
		d = -d;
	return 0.02*d+0.1;
}

static inline int get_hour(struct node *n, struct sim_state *s){
	int h = s->now/3600.0;
	struct def_user *d = n->user_data;
	return (h+d->time_zone)%24;
}
