#pragma once

#include "data.h"
#include "uthash.h"

struct resource_model {
	//Accumulated prob of this resource type
	//Mean and variance of resource length
	double prob, lm, lvar;
	//Mean and variance of bit rate
	double brm, brvar;
	//Mean and variance of time between releases
	double tm, tvar;
	struct skip_list_head models;
};

struct resource_provider {
	struct resource *r;
	int node_id;
	UT_hash_handle hh;
};

struct resource_entry {
	id_t resource_id;
	double prob;
	struct list_head probs;
	struct resource_provider *holders;
	UT_hash_handle hh;
};

static inline int resource_model_cmp(struct skip_list_head *h, void *key){
	struct resource_model *rm = skip_list_entry(h, struct resource_model, models);
	double k = *(double *)key;
	if (rm->prob < k-eps)
		return -1;
	if (rm->prob > k+eps)
		return 1;
	return 0;
}

void resource_add_provider(id_t rid, struct node *n, struct sim_state *s);
id_t new_resource(struct resource_model *r, struct sim_state *s);
void next_resource_event(struct sim_state *s);
