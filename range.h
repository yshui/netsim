#pragma once

#include "data.h"
#include "store.h"
#include "event.h"

void range_calc_flow_events(struct flow *f, double now);
void range_merge_with_next(struct range *rng, struct sim_state *s);
void range_calc_and_queue_event(struct flow *f, struct sim_state *s);

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

static inline struct range *
range_get(struct resource *rsrc, int start){
	struct skip_list_head *s = &rsrc->ranges, *r;
	struct range *rng;
	r = skip_list_find(s, &start, range_list_cmp);
	rng = skip_list_entry(r, struct range, ranges);
	if (!r)
		return NULL;
	if (rng->start > start)
		return NULL;
	return rng;
}

static inline void range_update_consumer_events(struct range *rng, struct sim_state *s){
	struct flow *f;
	list_for_each_entry(f, &rng->consumers, consumers)
		range_calc_and_queue_event(f, s);
}

static inline struct range *
resource_new_range(struct resource *r, size_t start, size_t len){
	struct range *rng = range_get(r, start);
	if (rng)
		return NULL;

	rng = range_new(start, len);
	rng->total_len = r->len;
	rng->owner = r->owner;
	skip_list_insert(&r->ranges, &rng->ranges, &rng->start, range_list_cmp);
	return rng;
}


static inline struct range *node_new_range(struct node *n, int resource_id,
					   size_t start, size_t len){
	struct resource *r = store_get(n->store, resource_id);
	return resource_new_range(r, start, len);
}

static inline void range_update(struct range *r, struct sim_state *s){
	//Kaham summation algo
	if (s->now == r->last_update)
		return;
	assert(s->now > r->last_update);
	double delta = r->producer->speed[1]*(s->now-r->last_update), t;
	delta = delta - r->lenc;
	t = delta+r->len;
	r->lenc = (t-r->len)-delta;
	r->len = t;
	r->last_update = s->now;
}
