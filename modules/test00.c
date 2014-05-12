#define LOG_DOMAIN "test"

#include "log.h"
#include "data.h"
#include "sim.h"
#include "store.h"
#include "user.h"

double test_delay(void *a, void *b){
	return 0.2;
}

double test_bandwidth(void *a, void *b){
	return 500;
}

void test_user_done(struct event *e, struct sim_state *s){
	user_done(e, s);
	log_info("Download done at %lfs\n", s->now);
}

void test00_init(struct sim_state *s){
	s->dlycalc = test_delay;
	s->bwcalc = test_bandwidth;

	struct node *server = sim_create_node(s);
	server->user_data = talloc(1, struct def_user);
	struct node *client = sim_create_node(s);
	client->user_data = talloc(1, struct def_user);

	sim_register_handler(FLOW_DONE, HNDR_USER, test_user_done, s);

	server->maximum_bandwidth[0] = server->maximum_bandwidth[1] = 5000;
	client->maximum_bandwidth[0] = client->maximum_bandwidth[1] = 1000;

	struct resource *r = sim_node_new_resource(server, 5000000, s);

	sim_establish_flow(r->resource_id, 0, server, client, s);

}
