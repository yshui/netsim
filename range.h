#pragma once

#include "data.h"
#include "store.h"
#include "event.h"

struct range *range_get(struct resource *rsrc, int start);
void range_calc_flow_events(struct flow *f);
void range_merge_with_next(struct range *rng);

__attribute__((pure))
static inline int range_list_cmp(struct skip_list_head *a, void *_key){
	struct range *rng = skip_list_entry(a, struct range, ranges);
	int key = *(int *)_key;
	if (rng->start <= key) {
		if (rng->start+rng->len > key)
			return 0;
		else
			return -1;
	} else
		return 1;
}

static inline void range_update_consumer_events(struct range *rng, struct sim_state *s){
	struct flow *f;
	list_for_each_entry(f, &rng->consumers, consumers){
		event_remove(f->done);
		event_remove(f->drain);
		free(f->done);
		free(f->drain);

		range_calc_flow_events(f);

		event_add(f->done, s);
		event_add(f->drain, s);
	}
}

static inline struct range *
resource_new_range(struct resource *r, size_t start, size_t len){
	struct range *rng = range_get(r, start);
	if (rng)
		return NULL;

	rng = range_new(start, len);
	rng->total_len = r->len;
	skip_list_insert(&r->ranges, &rng->ranges, &rng->start, range_list_cmp);
	return rng;
}


static inline struct range *node_new_range(struct node *n, int resource_id,
					   size_t start, size_t len){
	struct resource *r = store_get(n->store, resource_id);
	return resource_new_range(r, start, len);
}
