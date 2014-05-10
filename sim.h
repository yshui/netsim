#pragma once

#include "data.h"

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

