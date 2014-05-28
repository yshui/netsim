#include <unistd.h>

#define LOG_DOMAIN "p2p"

#include "client_behaviour.h"
#include "cloud_behaviour.h"
#include "record.h"

struct resource_model rrm[] = {
	{0.2, 15, 0, 300, 0},
	{0.3, 60, 20, 300, 10},
	{0.5, 5, 0, 300, 10},
};
const int nrm = 3;

void p2p_user_event(struct event *e, struct sim_state *s){
	struct user_event *ue = e->data;
	struct def_user *d = ue->d;
	id_t rid;
	switch(ue->type) {
		case NEW_CONNECTION:
			rid = resource_picker(s);
			client_new_play1(rid, ue->data, s);
			break;
		case NEW_RESOURCE:
			rid = new_resource_random(s);
			new_resource_handler1(rid, s);
			next_resource_event(s);
			break;
		case SIM_END:
			sim_end(s);
			log_info("[%.06lf] End simulation.\n", s->now);
			break;
		default:
			client_next_state_from_event(e, s);
			log_info("[%.06lf] Client %d %s -> %s, rid %d\n", s->now, d->n->node_id,
				 strstate(d->n->state), strstate(d->next_state), d->resource);
			if (ue->type == DONE_PLAY)
				client_next_event(d->n, s);
			client_handle_next_state(d->n, s);
	}
}

struct p2p_data {
	struct def_sim d;
	int new_resource_handler;
	int client_new_play;
	int end_simulation;
	int nsvr, ncld, nclnt;
};

void p2p_read_config(struct p2p_data *d){
	FILE *cfg = fopen("p2p.cfg", "r");
	fscanf(cfg, "%d", &d->d.max_rsrc);
	fscanf(cfg, "%d", &d->new_resource_handler);
	fscanf(cfg, "%d", &d->client_new_play);
	//Always assume uniformed distribution of cloud, sever and client
	//over timezones.
	fscanf(cfg, "%d", &d->nsvr);
	fscanf(cfg, "%d", &d->ncld);
	fscanf(cfg, "%d", &d->nclnt);
	fscanf(cfg, "%d", &d->end_simulation);
	fclose(cfg);
}

int p2p_init(struct sim_state *s){
	init_sim_size(s, 20, sizeof(struct p2p_data));

	struct p2p_data *pd;
	pd = s->user_data;
	p2p_read_config(pd);
	unlink("p2p_record");
	open_record("p2p_record", 1, s);
	s->dlycalc = distance_based_delay;
	s->bwcalc = distance_based_bw;

	struct def_sim *ds = s->user_data;
	ds->tvar = 20;
	ds->tm = 1800;
	int i;
	for(i = 1; i < nrm; i++) {
		rrm[i].prob += rrm[i-1].prob;
		skip_list_insert(&ds->rms, &rrm[i].models, &rrm[i].prob, resource_model_cmp);
	}

	for(i = 0; i < pd->nsvr; i++) {
		struct node *n = p2p_new_server(s);
		struct def_user *d = n->user_data;
		n->maximum_bandwidth[0] = 160000;
		n->maximum_bandwidth[1] = 320000;
		d->time_zone = 24*i/pd->d.nsvr;
	}

	for(i = 0; i < pd->ncld; i++) {
		struct node *cn = p2p_new_cloud(s);
		struct def_user *d = cn->user_data;
		cloud_online(cn, s);
		cn->maximum_bandwidth[0] = 80000;
		cn->maximum_bandwidth[1] = 80000;
		d->time_zone = 24*i/pd->d.ncld;
	}
	id_t rid = new_resource_random(s);
	new_resource_handler1(rid, s);
	next_resource_event(s);

	for(i = 0; i < pd->nclnt; i++) {
		struct node *n = p2p_new_node(s);
		struct def_user *d = n->user_data;
		n->state = N_IDLE;
		n->maximum_bandwidth[0] = 40000;
		n->maximum_bandwidth[1] = 8000;
		d->time_zone = 24*i/pd->nclnt;
		d->lowwm = 0;
		d->highwm = 2500;
		client_next_event(n, s);
	}

	sim_register_handler(FLOW_DONE, HNDR_USER, client_done, s);
	sim_register_handler(FLOW_SPEED_THROTTLE, HNDR_USER, client_speed_throttle, s);
	sim_register_handler(USER, HNDR_USER, p2p_user_event, s);
	sim_register_handler(SPEED_CHANGE, HNDR_USER, client_speed_change, s);

	struct user_event *ue = talloc(1, struct user_event);
	ue->type = SIM_END;
	struct event *e = event_new(100000, USER, ue);
	event_add(e, s);
	return 0;
}
