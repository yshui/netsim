#pragma once

#include "data.h"
#include "store.h"
#include "event.h"

void range_calc_and_requeue_events(struct flow *f, struct sim_state *s);
void range_merge_with_next(struct range *rng, struct sim_state *s);

__attribute__((pure))
static inline int range_include_cmp(struct skip_list_head *a, void *_key){
	struct range *rng = skip_list_entry(a, struct range, ranges);
	int key = *(int *)_key;
	if (rng->start <= key) {
		if (rng->start+rng->len >= key)
			return 0;
		else
			return -1;
	} else
		return 1;
}
__attribute__((pure))
static inline int range_start_cmp(struct skip_list_head *a, void *_key){
	struct range *rng = skip_list_entry(a, struct range, ranges);
	int key = *(int *)_key;
	return rng->start-key;
}

//Which [rng->start, rng->start+rng->len) is (start) IN
static inline struct range *
range_get(struct resource *rsrc, int start){
	struct skip_list_head *s = &rsrc->ranges, *r;
	struct range *rng;
	r = skip_list_find_ge(s, &start, range_include_cmp);
	rng = skip_list_entry(r, struct range, ranges);
	if (!r)
		return NULL;
	if (rng->start > start)
		return NULL;
	return rng;
}

static inline struct range *
range_get_by_start(struct resource *rsrc, int start){
	struct skip_list_head *s = &rsrc->ranges, *r;
	struct range *rng, *nrng;
	r = skip_list_find_le(s, &start, range_start_cmp);
	rng = skip_list_entry(r, struct range, ranges);
	if (!r)
		return NULL;
	return rng;
}

static inline struct range *
range_get_next(struct resource *rsrc, int start){
	struct skip_list_head *s = &rsrc->ranges, *r;
	struct range *rng, *nrng;
	r = skip_list_find_ge(s, &start, range_start_cmp);
	rng = skip_list_entry(r, struct range, ranges);
	if (!r)
		return NULL;
	return rng;
}

static inline void range_update_consumer_events(struct range *rng, struct sim_state *s){
	struct flow *f;
	list_for_each_entry(f, &rng->consumers, consumers)
		range_calc_and_requeue_events(f, s);
}

static inline struct range *
resource_new_range(struct resource *r, size_t start, size_t len){
	//Make sure the range doesn't overlap with another one
	assert(!range_get(r, start));
	assert(!range_get(r, start+len));

	struct range *rng = range_new(start, len);
	rng->total_len = r->len;
	rng->owner = r;
	skip_list_insert(&r->ranges, &rng->ranges, &rng->start, range_start_cmp);
	return rng;
}


static inline struct range *node_new_range(struct node *n, int resource_id,
					   size_t start, size_t len){
	struct resource *r = store_get(n->store, resource_id);
	return resource_new_range(r, start, len);
}

static inline void _range_update(struct range *r, struct sim_state *s){
	//Kaham summation algo
	if (s->now == r->last_update)
		return;
	if (r->producer == NULL) {
		//This range won't change
		r->last_update = s->now;
		return;
	}
	assert(s->now > r->last_update);
	double delta = r->producer->speed[1]*(s->now-r->last_update), t;
	delta = delta - r->lenc;
	t = delta+r->len;
	r->lenc = (t-r->len)-delta;
	r->len = t;
	r->last_update = s->now;
#ifndef NDEBUG
	//Predict source range len
	double pslen = 0;
	if (r->producer->srng->producer) {
		struct range *tmp = r->producer->srng;
		delta = tmp->producer->speed[1]*(s->now-tmp->last_update);
		pslen = tmp->len+delta;
	}else
		pslen = r->producer->srng->len;
	assert(r->start+r->len < r->producer->srng->start+pslen+eps);
#endif
}

static inline void flow_range_update(struct flow *f, struct sim_state *s){
	if (!f)
		return;
	_range_update(f->srng, s);
	_range_update(f->drng, s);
}

static inline void range_freep(struct skip_list_head *h){
	struct range *r = skip_list_entry(h, struct range, ranges);
	//Make sure we can free this range
	assert(!r->producer);
#ifndef NDEBUG
	struct flow *f;
	list_for_each_entry(f, &r->consumers, consumers){
		assert(false);
	}
#endif
	skip_list_deinit_node(&r->ranges);
	free(r);
}

static inline void range_clear(struct resource *r){
	skip_list_clear(&r->ranges, range_freep);
}
