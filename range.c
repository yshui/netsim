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
	return r ? skip_list_entry(r, struct range, ranges) : NULL;
}

//Calculate flow events. The flow structure must be fully populated.
//Done event is when the drng reaches next range's beginning.
//Drain event is when the end of srng is RECEIVED, so the delay should be
//considered as well.
//(note after drain event the srng may still be growing, so instead of closing
//the connection, we throttle the send speed).
void range_calc_flow_events(struct flow *f, double now){
	f->drain = f->done = NULL;
	if (f->bandwidth < eps)
		return;
	struct range *srng = f->srng;
	struct range *drng = f->drng;
	assert(srng->last_update == drng->last_update);
	struct skip_list_head *nh = srng->ranges.next[0];
	int npos;
	//The flow always appends to a range
	int drng_start = f->drng->start+f->drng->len-srng->start;
	double drain_time = (srng->len-drng_start)/
			    (double)(srng->grow-f->bandwidth);

	if (!srng->producer)
		//Don't have a producer, generate a DRAIN event
		f->drain = event_new(now+(srng->len-drng_start)/f->bandwidth,
				     FLOW_DRAIN, f);
	else if (drain_time < srng->producer->done->time &&
		 srng->grow < f->bandwidth+eps)
		//Otherwise generate a SPEED_THROTTLE, even if srng->grow == 0
		f->drain = event_new(now+drain_time, FLOW_SPEED_THROTTLE, f);

	assert(drng->producer == f);
	nh = drng->ranges.next[0];
	if (nh) {
		struct range *nrng = skip_list_entry(nh, struct range, ranges);
		npos = nrng->start;
	}else
		npos = drng->total_len;
	double done_time = (npos-drng->start-drng->len)/(double)f->bandwidth;
	//Less or equal to here, we always handle done event first. Since when
	//its done, we don't need to deal with drain or throttle.
	if (done_time < f->drain->time+eps) {
		free(f->drain);
		f->drain = NULL;
		f->done = event_new(now+done_time, FLOW_DONE, f);
	}else
		f->done = NULL;
}

//Merge a range with its successor, must be called after every ranges' length
//is updated.
void range_merge_with_next(struct range *rng, struct sim_state *s){
	//Size range size to len+size
	struct skip_list_head *nh = rng->ranges.next[0];
	if (!nh)
		return;

	struct range *nrng = skip_list_entry(nh, struct range, ranges);
	assert(rng->start+rng->len == nrng->start);
	rng->len = nrng->start-rng->start+nrng->len;
	rng->grow = nrng->grow;

	struct flow *f;
	list_for_each_entry(f, &rng->consumers, consumers){
		event_remove(f->drain);
		event_remove(f->done);
		free(f->drain);
		free(f->done);
		range_calc_flow_events(f, s->now);
		event_add(f->drain, s);
		event_add(f->done, s);
	}

	list_for_each_entry(f, &nrng->consumers, consumers){
		event_remove(f->drain);
		event_remove(f->done);
		free(f->drain);
		free(f->done);
		range_calc_flow_events(f, s->now);
		event_add(f->drain, s);
		event_add(f->done, s);
	}

	while(!list_empty(&nrng->consumers)){
		struct list_head *h = nrng->consumers.next;
		list_del(h);
		list_add(h, &rng->consumers);
	}

	skip_list_delete_next(&rng->ranges);
}
