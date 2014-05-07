#pragma once

#include "data.h"
#include "range.h"
#include "store.h"
#include "skiplist.h"
#include "record.h"

static inline struct range *node_new_range(struct node *n, int resource_id,
					   int start){
	struct resource *r = store_get(n->store, resource_id);
	struct range *rng = range_new(start);
	rng->total_len = r->len;
	skip_list_insert(&r->ranges, &rng->ranges, &rng->start, range_list_cmp);
	return rng;
}
