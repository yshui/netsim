#pragma once
#include "resource.h"
#include "sim.h"
#include "common.h"

struct def_user {
	double bit_rate;
	enum node_state next_state;
	void *trigger;
	//low water mark: stop playing
	//high water mark: start playing
	int highwm, lowwm;
	int buffer_pos;
	int resource;
	double last_update, last_speed;
	struct node *n;
	struct event *e;
	struct resource_provider *p;
};

struct server {
	struct node *n;
	struct list_head servers;
};

enum ue_type {
	DONE_PLAY,
	PAUSE_BUFFERING,
	DONE_BUFFERING,
	NEW_CONNECTION,
	NEW_RESOURCE,
};

struct user_event {
	int type;
	struct def_user *d;
	void *data;
};

struct def_sim {
	//Resource with hit prob
	//The prob is estimated using zipf distribution
	//See: http://home.ifi.uio.no/~griff/papers/62.pdf
	struct list_head rsrc_probs;
	struct list_head servers;
	struct skip_list_head rms;
	struct resource_entry *rsrcs;
	//resource number limit
	int max_rsrc, nrsrc;
};

static inline struct node *
p2p_new_node(struct sim_state *s){
	struct node *n = sim_create_node(s);
	struct def_user *d;
	n->user_data = d = (struct def_user *)talloc(1, struct def_user);
	d->n = n;
	return n;
}

static inline struct node *
p2p_new_server(struct sim_state *s){
	struct node *n = p2p_new_node(s);
	struct def_sim *ds = s->user_data;
	n->state = N_SERVER;
	struct server *ss = talloc(1, struct server);
	ss->n = n;
	list_add(&ss->servers, &ds->servers);
	return n;
}

static inline void
init_sim(struct sim_state *s, int max){
	struct def_sim *ds = talloc(1, struct def_sim);
	INIT_LIST_HEAD(&ds->servers);
	INIT_LIST_HEAD(&ds->rsrc_probs);
	skip_list_init_head(&ds->rms);
	ds->max_rsrc = max;
	s->user_data = ds;
}
