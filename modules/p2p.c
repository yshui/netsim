#include <unistd.h>

#define LOG_DOMAIN "p2p"

#include "client_behaviour.h"
#include "cloud_behaviour.h"
#include "record.h"

struct resource_model rrm[] = {
	{0.2, 900, 3, 2000, 100, 871, 450},
	{0.3, 3600, 20, 2400, 0, 3791, 300},
	{0.5, 300, 2, 2000, 500, 337, 60},
};

struct p2p_data {
	struct def_sim d;
	int new_resource_handler;
	int client_new_play;
	int new_connection_handler;
	int smart_cloud;
	int use_client;
	int metric;
	int end_simulation;
	int nsvr, ncld, nclnt;
};

const int nrm = 3;

static void (*cloud_new_rsrc_func)(id_t, bool, struct sim_state *s);

void p2p_user_event(struct event *e, struct sim_state *s){
	struct user_event *ue = e->data;
	struct user_event *need_free = ue;
	struct def_user *d = ue->d;
	struct p2p_data *ds = s->user_data;
	id_t rid;
	switch(ue->type) {
		case NEW_CONNECTION:
			rid = resource_picker(s);
			if (!ds->client_new_play)
				client_new_play1(rid, ue->data, s);
			else
				client_new_play2(rid, ue->data, ds->use_client, s);
			break;
		case NEW_RESOURCE:
			rid = new_resource_random(s);
			cloud_new_rsrc_func(rid, ds->new_resource_handler , s);
			next_resource_event(s);
			break;
		case SIM_END:
			sim_end(s);
			log_info("[%.06lf] End simulation.\n", s->now);
			break;
		case HOUR_PASS:
			cloud_next_hour_handler(s);
			need_free = NULL;
			e = event_new(s->now+60*60, USER, ue);
			event_add(e, s);
			break;
		default:
			need_free = NULL;
			client_next_state_from_event(e, s);
			log_info("[%.06lf] Client %d %s -> %s, rid %d\n", s->now, d->n->node_id,
				 strstate(d->n->state), strstate(d->next_state), d->resource);
			if (ue->type == DONE_PLAY)
				client_next_event(d->n, s);
			client_handle_next_state(d->n, s);
	}
	free(need_free);
}

#define SKIPd "%*[^0-9]%d"

void p2p_read_config(struct p2p_data *d){
	FILE *cfg = fopen("p2p.cfg", "r");
	fscanf(cfg, SKIPd, &d->d.max_rsrc);
	fscanf(cfg, SKIPd, &d->new_resource_handler);
	fscanf(cfg, SKIPd, &d->client_new_play);
	fscanf(cfg, SKIPd, &d->smart_cloud);
	fscanf(cfg, SKIPd, &d->new_connection_handler);
	fscanf(cfg, SKIPd, &d->use_client);
	fscanf(cfg, SKIPd, &d->metric);
	//Always assume uniformed distribution of cloud, sever and client
	//over timezones.
	fscanf(cfg, SKIPd, &d->nsvr);
	fscanf(cfg, SKIPd, &d->ncld);
	fscanf(cfg, SKIPd, &d->nclnt);
	fscanf(cfg, SKIPd, &d->end_simulation);
	fclose(cfg);

	d->d.push_metric = distance_metric;
	if (d->metric)
		d->d.fetch_metric = share_metric;
	else
		d->d.fetch_metric = distance_metric;
	cloud_new_rsrc_func = new_resource_handler1;

	switch(d->new_connection_handler) {
		case 0:
			d->d.new_conn_cb = NULL;
			break;
		case 1:
			d->d.new_conn_cb = new_connection_handler1;
			break;
		case 2:
			d->d.new_conn_cb = new_connection_handler2;
			break;
		default:
			assert(false);
	}
}

void p2p_done(struct event *e, struct sim_state *s){
	client_done(e, s);
	struct flow *f = e->data;
	struct p2p_data *d = s->user_data;
	struct def_user *sd = f->peer[0]->user_data;
	if (sd->nt == N_CLOUD)
		cloud_flow_done(f->peer[0], f->peer[1], f->resource_id, s);
}

int p2p_init(struct sim_state *s){
	srandom(time(0));
	init_sim_size(s, 20, sizeof(struct p2p_data));

	struct p2p_data *pd;
	pd = s->user_data;
	p2p_read_config(pd);
	unlink("p2p_record");
	open_record("p2p_record", 1, s);
	s->dlycalc = distance_based_delay;
	s->bwcalc = distance_based_bw;

	struct def_sim *ds = s->user_data;
	int i;
	for(i = 1; i < nrm; i++) {
		rrm[i].prob += rrm[i-1].prob;
		skip_list_insert(&ds->rms, &rrm[i].models, &rrm[i].prob, resource_model_cmp);
	}

	for(i = 0; i < pd->nsvr; i++) {
		struct node *n = p2p_new_server(s);
		struct def_user *d = n->user_data;
		n->maximum_bandwidth[0] = 160000;
		n->maximum_bandwidth[1] = 160000;
		d->time_zone = 24*i/pd->nsvr;
	}

	for(i = 0; i < pd->ncld; i++) {
		struct node *cn = p2p_new_cloud(s);
		struct def_user *d = cn->user_data;
		cn->maximum_bandwidth[0] = 120000;
		cn->maximum_bandwidth[1] = 120000;
		d->time_zone = 24*i/pd->ncld;
		if (!pd->smart_cloud)
			//Cloud is always online if not smart_cloud
			cloud_online(cn, s);
		else
			d->type = 1;
	}
	id_t rid = new_resource_random(s);
	new_resource_handler1(rid, false, s);
	next_resource_event(s);

	for(i = 0; i < pd->nclnt; i++) {
		struct node *n = p2p_new_node(s);
		struct def_user *d = n->user_data;
		n->state = N_IDLE;
		n->maximum_bandwidth[0] = 8000;
		n->maximum_bandwidth[1] = 40000;
		d->time_zone = 24*i/pd->nclnt;
		d->lowwm = 0;
		d->highwm = 25000;
		//Randomize first event
		struct user_event *ue = talloc(1, struct user_event);
		ue->type = NEW_CONNECTION;
		ue->data = n;
		struct event *e = event_new(gaussian_noise_nz(900, 3600), USER, ue);
		log_info("CLNT %lf\n", e->time);
		event_add(e, s);
	}

	sim_register_handler(FLOW_DONE, HNDR_USER, p2p_done, s);
	sim_register_handler(FLOW_SPEED_THROTTLE, HNDR_USER, client_speed_throttle, s);
	sim_register_handler(USER, HNDR_USER, p2p_user_event, s);
	sim_register_handler(SPEED_CHANGE, HNDR_USER, client_speed_change, s);

	struct user_event *ue = talloc(1, struct user_event);
	ue->type = SIM_END;
	struct event *e = event_new(pd->end_simulation, USER, ue);
	e->auto_free = true;
	event_add(e, s);

	if (pd->new_resource_handler) {
		//Add next_hour event
		ue = talloc(1, struct user_event);
		ue->type = HOUR_PASS;
		e = event_new(60*10, USER, ue);
		e->auto_free = true;
		event_add(e, s);
	}
	return 0;
}
