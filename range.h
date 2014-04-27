#pragma once

#include "data.h"

struct range *range_get(struct resource *rsrc, int start);
void range_calc_flow_events(struct flow *f);
void range_merge_with_next(struct range *rng);

__attribute__((pure))
static inline int range_list_cmp(struct skip_list_head *a, void *_key){
	struct range *rng = skip_list_entry(a, struct range, ranges);
	int key = *(int *)_key;
	if (!rng)
		return 1;
	if (rng->start <= key) {
		if (rng->start+rng->len > key)
			return 0;
		else
			return -1;
	} else
		return 1;
}

static inline void range_update_consumer_events(struct range *rng){
	struct flow *f;
	list_for_each_entry(f, &rng->consumers, consumers)
		range_calc_flow_events(f);
}
