#pragma once

#include "data.h"
#include "range.h"
#include "store.h"
#include "skiplist.h"
#include "statistic.h"

static inline struct range *node_new_range(struct node *n, int resource_id,
					   int start){
	struct resource *r = store_get(n->store, resource_id);
	struct range *rng = range_new(start);
	rng->total_len = r->len;
	skip_list_insert(&r->ranges, &rng->ranges, &rng->start, range_list_cmp);
	return rng;
}

static inline void node_record_speed(struct node *n, struct connection *c,
				     int dir, struct sim_state *s){
	write_record(0, R_BANDWIDTH_USAGE|dir, n->node_id, -1,
		     n->bandwidth_usage[dir]);

	write_record(0, R_SPEED_CHANGE|dir, n->node_id, -1, c->speed[dir]
}
