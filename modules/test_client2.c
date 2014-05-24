#define LOG_DOMAIN "testc"
#include "client_behaviour.h"
#include "log.h"
#include "sim.h"
#include "user.h"
#include "event.h"

static struct node *s1, *s2, *c1, *c2;
static id_t rid;

struct user_def{
	struct def_user d;
	void *loc;
};

struct node *test_create_node(struct sim_state *s){
	struct node *n = sim_create_node(s);
	struct def_user *d;
	n->user_data = d = (struct def_user *)talloc(1, struct user_def);
	d->n = n;
	return n;
}

void test_user_done(struct event *e, struct sim_state *s){
	client_done(e, s);
}

void test_sc(struct event *e, struct sim_state *s){
	client_speed_change(e, s);
}

void test_user_event(struct event *e, struct sim_state *s){
	struct user_event *ue = e->data;
	struct def_user *d = ue->d;
	if (ue->type == NEW_CONNECTION) {
		client_new_connection(rid, 0, s1, c2, s);
		client_start_play(c2, rid, s);
		return;
	}
	client_next_state_from_event(e, s);
	log_info("[%.06lf] Client: %d, %s -> %s\n", s->now, d->n->node_id,
		 strstate(d->n->state), strstate(d->next_state));
	client_handle_next_state(d->n, s);
}

double test_delay(void *a, void *b){
	return 0.2;
}

double test_bandwidth(void *a, void *b){
	return 100;
}

int tc2_init(struct sim_state *s){
	init_sim(s, 10);
	struct def_sim *ds = s->user_data;
	s->dlycalc = test_delay;
	s->bwcalc = test_bandwidth;

	s1 = p2p_new_server(s);
	c1 = test_create_node(s);
	c2 = test_create_node(s);

	sim_register_handler(FLOW_DONE, HNDR_USER, test_user_done, s);
	sim_register_handler(USER, HNDR_USER, test_user_event, s);
	sim_register_handler(SPEED_CHANGE, HNDR_USER, test_sc, s);

	s1->maximum_bandwidth[0] = s1->maximum_bandwidth[1] = 100;
	c1->maximum_bandwidth[0] = c1->maximum_bandwidth[1] = 100;
	c2->maximum_bandwidth[0] = c2->maximum_bandwidth[1] = 100;

	struct resource_model *rm = talloc(1, struct resource_model);
	rm->lvar = 0;
	rm->lm = 15;
	rm->brvar = 0;
	rm->brm = 300;
	rm->prob = 1;
	skip_list_insert(&ds->rms, &rm->models, &rm->prob, resource_model_cmp);

	rid = new_resource(rm, s);
	struct def_user *d = c1->user_data;
	d->lowwm = 1000;
	d->highwm = 2000;
	d = c2->user_data;
	d->lowwm = 1000;
	d->highwm = 2000;

	c1->state = N_IDLE;
	c2->state = N_IDLE;
	s1->state = N_SERVER;

	client_new_connection(rid, 0, s1, c1, s);
	client_start_play(c1, rid, s);

	struct user_event *ue = talloc(1, struct user_event);
	ue->type = NEW_CONNECTION;
	ue->d = c2->user_data;
	ue->data = NULL;
	struct event *e = event_new(25, USER, ue);
	event_add(e, s);
	return 0;
}
