#include "data.h"
#include "record.h"
#include "p2p_common.h"

static inline int opt1(struct node *n, void *data){
	struct node *c = (struct node *)data;
	int ans;
	struct def_user *d1, *d2;
	d1 = n->user_data;
	d2 = c->user_data;
	ans = d1->time_zone-d2->time_zone;
	return ans < 0 ? -ans : ans;
}
void new_resource_handler1(struct event *e, struct sim_state *s){
	//Naive, fetch from nearest n/2 server
	struct def_sim *ds = s->user_data;
	int i;
	for(i = 0; i < ds->nsvr/2; i++){

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


