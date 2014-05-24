#include "client_behaviour.h"
#include "cloud_behaviour.h"

struct resource_model rrm[] = {
	{0.2, 15, 0, 300, 0},
	{0.3, 60, 20, 300, 10},
	{0.5, 5, 0, 300, 10},
};
const int nrm = 3;
const int ncloud = 16;
const int nuser = 100;
const int nsvr = 2;

void p2p_user_event(struct event *e, struct sim_state *s){
	struct user_event *ue = e->data;
	struct def_user *d = ue->d;
	struct node *n;
	switch(ue->type) {
		case NEW_CONNECTION:
			break;
	}
}

int p2p_init(struct sim_state *s){
	init_sim(s, 20);

	struct def_sim *ds = s->user_data;
	ds->tvar = 20;
	ds->tm = 36000;
	int i;
	for(i = 1; i < nrm; i++) {
		rrm[i].prob += rrm[i-1].prob;
		skip_list_insert(&ds->rms, &rrm[i].models, &rrm[i].prob, resource_model_cmp);
	}

	for(i = 0; i < nsvr; i++)
		p2p_new_server(s);

	for(i = 0; i < ncloud; i++) {
		struct node *cn = p2p_new_cloud(s);
		cloud_online(cn, s);
	}
	id_t rid = new_resource_random(s);
	new_resource_handler1(rid, s);
	next_resource_event(s);

	for(i = 0; i < nuser; i++) {
		struct node *n = p2p_new_node(s);
		n->state = N_IDLE;
		client_next_event(n, s);
	}
	return 0;
}
