#include "data.h"
#include "record.h"
#include "client_behaviour.h"
#include "cloud_behaviour.h"

void new_resource_handler1(id_t rid, struct sim_state *s){
	//Naive, fetch from nearest n/2 server
	struct def_sim *ds = s->user_data;
	struct cloud_node *cn;

	list_for_each_entry(cn, &ds->cloud_nodes, cloud_nodes) {
		int cnt = ds->nsvr/2, i;
		server_picker_opt1(cn->n, distance_metric, &cnt, cn->n, s);
		assert(cnt);
		struct resource *r = store_get(ds->eval_table[0].n->store, rid);
		int split = r->len/cnt;
		for(i = 0; i < cnt; i++)
			client_new_connection(rid, i*split, ds->eval_table[i].n, cn->n, s);
	}
}

void cloud_online(struct node *n, struct sim_state *s){
	//cloud nodes don't go away,
	//they just online/offline
	n->state = N_CLOUD;
	write_record(0, R_NODE_STATE, n->node_id, sizeof(n->state), &n->state, s);
}

void cloud_offline(struct node *n, struct sim_state *s){
	//cloud nodes don't go away,
	//they just online/offline
	n->state = N_OFFLINE;
	write_record(0, R_NODE_STATE, n->node_id, sizeof(n->state), &n->state, s);
}
