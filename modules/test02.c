#define LOG_DOMAIN "test"

#include "log.h"
#include "data.h"
#include "sim.h"
#include "store.h"
#include "user.h"
#include "event.h"
#include "connect.h"
#include "test_common.h"

static struct node *server, *client, *client2;
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
	log_info("Download done at %lfs, node %d \n", s->now, f->dst->node_id);
}

void test_user_event(struct event *e, struct sim_state *s){
	sim_establish_flow(r->resource_id, 0, server, client2, s);
}

void test_sc(struct event *e, struct sim_state *s){
	struct spd_event *se = e->data;
	log_info("Node %d dir %d speed %lf\n", se->c->peer[se->type]->node_id, se->type, se->speed);
}

void test02_init(struct sim_state *s){
	s->dlycalc = test_delay;
	s->bwcalc = test_bandwidth;

	server = test_create_node(s);
	client = test_create_node(s);
	client2 = test_create_node(s);
	log_info("Client1: %d Client2: %d\n", client->node_id, client2->node_id);

	sim_register_handler(FLOW_DONE, HNDR_USER, test_user_done, s);
	sim_register_handler(USER, HNDR_USER, test_user_event, s);
	sim_register_handler(SPEED_CHANGE, HNDR_USER, test_sc, s);

	server->maximum_bandwidth[0] = server->maximum_bandwidth[1] = 5000;
	client->maximum_bandwidth[0] = client->maximum_bandwidth[1] = 1000;
	client2->maximum_bandwidth[0] = client2->maximum_bandwidth[1] = 1000;

	r = sim_node_new_resource(server, 5000000);

	sim_establish_flow(r->resource_id, 0, server, client, s);

	struct event *e = event_new(5000, USER, NULL);
	event_add(e, s);

}
