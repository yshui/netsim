#pragma once

#include "data.h"
#include "sim.h"

static inline
struct node *test_create_node(struct sim_state *s){
	struct node *n = sim_create_node(s);
	n->user_data = NULL;
	return n;
}
