#define LOG_DOMAIN "test"

#include "log.h"
#include "data.h"
#include "sim.h"
#include "store.h"
#include "user.h"
#include "event.h"
#include "flow.h"
#include "record.h"
#include "test_common.h"

static struct node *s1, *s2, *c1, *c2;
static struct resource *r;

double test_delay(void *a, void *b){
	return 0.2;
}

double test_bandwidth(void *a, void *b){
	return 500000;
}

void test_user_done(struct event *e, struct sim_state *s){
	user_done(e, s);
	struct flow *f = e->data;
	log_info("[%.06lf] Download done , node %d \n", s->now, f->peer[1]->node_id);
	if (f->peer[0] == s1 && f->peer[1] == s2) {
		//Start s1->c1
		sim_establish_flow(f->resource_id, 0, s1, c1, s);
		struct event *e = event_new(s->now+0.1, USER, NULL);
		event_add(e, s);
	}
	struct resource *r = store_get(f->peer[1]->store, f->resource_id);
	print_range(r);
}

void test_user_event(struct event *e, struct sim_state *s){
	sim_establish_flow(r->resource_id, 0, s1, c2, s);
	sim_establish_flow(r->resource_id, 10000, s2, c1, s);
}

void test_sc(struct event *e, struct sim_state *s){
	struct spd_event *se = e->data;
	int t = se->type;
	log_info("[%.06lf] Node %d -> %d dir %d se->speed %lf result speed %lf\n",
		 s->now, se->f->peer[0]->node_id, se->f->peer[1]->node_id, t,
		 se->speed, se->f->speed[se->type]);
}

void test05_init(struct sim_state *s){
	open_record("R", 1, s);
	s->dlycalc = test_delay;
	s->bwcalc = test_bandwidth;

	s1 = test_create_node(s);
	s2 = test_create_node(s);
	c1 = test_create_node(s);
	c2 = test_create_node(s);
	log_info("Client1: %d Client2: %d\n", c1->node_id, c2->node_id);

	sim_register_handler(FLOW_DONE, HNDR_USER, test_user_done, s);
	sim_register_handler(USER, HNDR_USER, test_user_event, s);
	sim_register_handler(SPEED_CHANGE, HNDR_USER, test_sc, s);

	s1->maximum_bandwidth[0] = s1->maximum_bandwidth[1] = 1000;
	s2->maximum_bandwidth[0] = s2->maximum_bandwidth[1] = 1000;
	c1->maximum_bandwidth[0] = c1->maximum_bandwidth[1] = 1000;
	c2->maximum_bandwidth[0] = c2->maximum_bandwidth[1] = 1000;

	r = sim_node_new_resource(s1, 5000000);

	sim_establish_flow(r->resource_id, 0, s1, s2, s);
}
