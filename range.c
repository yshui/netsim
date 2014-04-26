#include <stdlib.h>
#include <assert.h>

#include "data.h"
#include "skiplist.h"
#include "common.h"
#include "event.h"
#include "range.h"

struct range *range_get(struct resource *rsrc, int start){
	struct skip_list_head *s = &rsrc->ranges, *r;
	r = skip_list_find(s, &start, range_list_cmp);
	return skip_list_entry(r, struct range, ranges);
}

//Calculate flow events. The flow structure must be fully populated.
void range_calc_flow_events(struct flow *f){
	struct range *srng = f->srng;
	struct skip_list_head *nh = srng->ranges.next[0];
	int npos;
	double drain_time = srng->len/(double)(srng->grow-f->bandwidth);

	f->drain = f->done = NULL;
	if (drain_time > srng->producer->done->time ||
	    srng->grow > f->bandwidth+eps) {
		if (!nh) {
			drain_time = (srng->total_len-srng->start)/(double)f->bandwidth;
			f->drain = event_new(drain_time, FLOW_DRAIN, f);
		}
	}else
		f->drain = event_new(drain_time, FLOW_SOURCE_THROTTLE, f);

	struct range *drng = f->drng;
	assert(drng->producer == f);
	nh = drng->ranges.next[0];
	if (nh) {
		struct range *nrng = skip_list_entry(nh, struct range, ranges);
		npos = nrng->start;
	}else
		npos = drng->total_len;
	double done_time = (npos-drng->start)/(double)f->bandwidth;
	if (done_time < f->drain->time) {
		free(f->drain);
		f->drain = NULL;
		f->done = event_new(done_time, FLOW_DONE, f);
	}else
		f->done = NULL;
}

//Merge a range with its successor, must be called after every ranges' length
//is updated.
void range_merge_with_next(struct range *rng){
	//Size range size to len+size
	struct skip_list_head *nh = rng->ranges.next[0];
	if (!nh)
		return;

	struct range *nrng = skip_list_entry(nh, struct range, ranges);
	assert(rng->start+rng->len == nrng->start);
	rng->len = nrng->start-rng->start+nrng->len;
	rng->grow = nrng->grow;

	struct flow *f;
	list_for_each_entry(f, rng->consumers, consumers){
		event_remove(f->drain);
		event_remove(f->done);
		range_calc_flow_events(f);
	}

	list_for_each_entry(f, nrng->consumers, consumers){
		event_remove(f->drain);
		event_remove(f->done);
		range_calc_flow_events(f);
	}

	while(!list_empty(nrng->consumers)){
		struct list_head *h = nrng->consumers;
		nrng->consumers = nrng->consumers->next;
		list_add(h, rng->consumers);
	}

	skip_list_delete_next(&rng->ranges);
}

void range_update_rcv_spd(struct connection *c){
	c->f->drng->grow = c->rcv_spd;
}
