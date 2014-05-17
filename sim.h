#pragma once

#include "data.h"
#include "log.h"

struct packet{
	struct node *src, *dst;
	void *data;
	int len;
};

struct node *sim_create_node(struct sim_state *s);
void sim_register_handler(int type, int priority, event_handler_func f,
			  struct sim_state *s);
int sim_establish_flow(id_t rid, size_t start, struct node *src, struct node *dst,
		       struct sim_state *s);
void sim_send_packet(void *data, int len, struct node *src, struct node *dst,
		     struct sim_state *s);
struct resource *
sim_node_new_resource(struct node *n, size_t len, struct sim_state *s);

static inline
void print_range(struct resource *r){
	struct skip_list_head *h = r->ranges.next[0];
	while(h){
		struct range *rng = skip_list_entry(h, struct range, ranges);
		log_info("Range [%u, %u)\n", rng->start, rng->start+rng->len);
		h = h->next[0];
	}
}
