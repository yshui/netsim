#include "data.h"
#include "record.h"
#include "client_behaviour.h"
#include "cloud_behaviour.h"

void new_resource_handler1(id_t rid, bool delay, struct sim_state *s){
	//Naive, fetch from nearest n/2 server
	struct def_sim *ds = s->user_data;
	struct cloud_node *cn;

	list_for_each_entry(cn, &ds->cloud_nodes, cloud_nodes) {
		if (delay && !is_busy_hour(get_hour(cn->n, s)))
			continue;
		int cnt = ds->nsvr/2, i;
		server_picker_opt1(cn->n, distance_metric, &cnt, cn->n, s);
		assert(cnt);
		struct resource *r = store_get(ds->eval_table[0].n->store, rid);
		int split = r->len/cnt;
		for(i = 0; i < cnt; i++)
			client_new_connection(rid, i*split, ds->eval_table[i].n, cn->n, s);
	}
}

static inline int _nv_cmp(const void *a, const void *b){
	struct nv_pair *_a = (struct nv_pair *)a;
	struct nv_pair *_b = (struct nv_pair *)b;
	return _a->val-_b->val;
}

void cloud_push1(id_t rid, struct node *src, struct sim_state *s, bool client){
	struct def_user *d = src->user_data;
	struct def_sim *ds = s->user_data;
	int c = 0;
	if (d->cloud_push_dst > ds->ncld/4) {
		if (client && src->bandwidth_usage[0] < src->maximum_bandwidth[0]) {
			//Don't push if at maximum usage
			struct def_user *d;
			list_for_each_entry(d, &ds->nodes, nodes){
				if (d->n->state == N_SERVER || d->n->state == N_CLOUD)
					continue;
				struct resource *r = store_get(d->n->store, rid);
				if (r)
					continue;
				ds->eval_table[c].n = d->n;
				ds->eval_table[c++].val = distance_metric(d->n, src);
			}
			qsort(ds->eval_table, c, sizeof(struct nv_pair), _nv_cmp);
			int i;
			for(i = 0; i < c; i++) {
				if (src->bandwidth_usage[0] > 0.9*src->maximum_bandwidth[0])
					break;
				client_new_connection(rid, 0, src, ds->eval_table[i].n, s);
			}
		}
		return;
	}
	struct cloud_node *cn;
	list_for_each_entry(cn, &ds->cloud_nodes, cloud_nodes){
		struct resource *r = store_get(cn->n->store, rid);
		if (r)
			continue;
		if (cn->n->state != N_CLOUD) {
			assert(cn->n->state == N_OFFLINE);
			cloud_online(cn->n, s);
		}
		ds->eval_table[c].n = cn->n;
		ds->eval_table[c++].val = distance_metric(cn->n, src);
	}
	if (c > ds->ncld/8) {
		qselect_eval(ds->eval_table, c, ds->ncld/8);
		c = ds->ncld/8;
	}
	int i;
	for (i = 0; i < c; i++)
		client_new_connection(rid, 0, src, ds->eval_table[i].n, s);
	d->cloud_push_dst += c;
}

struct _rsrc {
	int count;
	id_t id;
	UT_hash_handle hh;
};

static inline void new_connection_handler(struct node *cld, id_t rid,
					  bool client, struct sim_state *s){
	//Check bandwidth usage, and do resource pushing and stuff
	//client = if push to client as well
	struct _rsrc *r = NULL;
	if (cld->bandwidth_usage[0] < 0.7*cld->maximum_bandwidth[0])
		return;
	struct flow *tf;
	int max = 0;
	id_t mrid;
	list_for_each_entry(tf, &cld->conns[0], conns[0]){
		struct _rsrc *tr = 0;
		HASH_FIND_INT(r, &tf->resource_id, tr);
		if (!tr) {
			tr = talloc(1, struct _rsrc);
			tr->id = tf->resource_id;
			tr->count = 1;
			HASH_ADD_INT(r, id, tr);
		}else
			tr->count++;
		if (tr->count > max) {
			max = tr->count;
			mrid = tr->id;
		}
	}
	//Choose dst cloud nodes
	cloud_push1(rid, cld, s, client);
}

void new_connection_handler1(struct node *cld, id_t rid, struct sim_state *s){
	new_connection_handler(cld, rid, false, s);
}

void new_connection_handler2(struct node *cld, id_t rid, struct sim_state *s){
	new_connection_handler(cld, rid, true, s);
}

void cloud_flow_done(struct node *cld, id_t rid, struct sim_state *s){
}

void cloud_online(struct node *n, struct sim_state *s){
	//cloud nodes don't go away,
	//they just online/offline
	struct def_user *d = n->user_data;
	struct def_sim *ds = s->user_data;
	assert(n->state == N_CLOUD || n->state == N_OFFLINE);
	if (n->state != N_OFFLINE)
		return;
	sim_node_change_state(n, N_CLOUD, s);
	d->next_state = n->state;
	ds->ncld_on++;
}

void cloud_offline(struct node *n, struct sim_state *s){
	//cloud nodes don't go away,
	//they just online/offline
	struct def_user *d = n->user_data;
	struct def_sim *ds = s->user_data;
	assert(n->state == N_CLOUD || n->state == N_OFFLINE);
	if (n->state != N_CLOUD)
		return;
	sim_node_change_state(n, N_OFFLINE, s);
	d->next_state = n->state = N_OFFLINE;
	ds->ncld_on--;
}
