#include <stdlib.h>
#include <assert.h>

#include "data.h"
#include "skiplist.h"
#include "common.h"
#include "event.h"
#include "range.h"

//Calculate flow events. The flow structure must be fully populated.
//Done event is when the drng reaches next range's beginning.
//Drain event is when the end of srng is RECEIVED, so the delay should be
//considered as well.
//(note after drain event the srng may still be growing, so instead of closing
//the connection, we throttle the send speed).
void range_calc_and_requeue_events(struct flow *f, struct sim_state *s){
	if (!f)
		return;
	event_remove(f->done);
	event_remove(f->drain);
	flow_range_update(f, s);
	if (!f->drain)
		f->drain = event_new(0, 0, f);
	if (!f->done)
		f->done = event_new(0, 0, f);
	if (f->speed[1] < eps)
		return;
	struct range *srng = f->srng;
	struct range *drng = f->drng;
	assert(srng->last_update == drng->last_update);
	struct skip_list_head *nh = srng->ranges.next[0];
	int npos;
	double sgrow = srng->producer ? srng->producer->speed[1] : 0;
	double fbw = f->speed[1];
	//The flow always appends to a range
	double drng_start = f->drng->start+f->drng->len-srng->start;
	assert(srng->len > drng_start-eps);
	double drain_time = (srng->len-drng_start)/(fbw-sgrow);

	if (!srng->producer) {
		//Don't have a producer, generate a DRAIN event
		f->drain->time = s->now+(srng->len-drng_start)/fbw;
		f->drain->type = FLOW_DRAIN;
		event_add(f->drain, s);
	} else if (!is_later_than(drain_time+s->now, srng->producer->done) &&
		   sgrow < fbw) {
		f->drain->type = FLOW_SPEED_THROTTLE;
		if (fequ(srng->len, drng_start)) {
			//Generate throttle right now
			if (!fequ(sgrow, fbw)) {
				f->drain->time = s->now;
				event_add(f->drain, s);
			}
		} else {
			//Not Already throttled
			//sgrow == fbw would be fine
			//Otherwise generate a SPEED_THROTTLE, even if srng->grow == 0
			f->drain->time = s->now+drain_time;
			event_add(f->drain, s);
		}
	}

	assert(drng->producer == f);
	nh = drng->ranges.next[0];
	if (nh) {
		struct range *nrng = skip_list_entry(nh, struct range, ranges);
		npos = nrng->start;
	}else
		npos = drng->total_len;
	double done_time = (npos-drng->start-drng->len)/(double)fbw;
	//Less or equal to here, we always handle done event first. Since when
	//its done, we don't need to deal with drain or throttle.
	if (!is_later_than(s->now+done_time, f->drain)) {
		event_remove(f->drain);
		f->done->time = s->now+done_time;
		f->done->type = FLOW_DONE;
		event_add(f->done, s);
	}
}

//Merge a range with its successor, must be called after every ranges' length
//is updated.
void range_merge_with_next(struct range *rng, struct sim_state *s){
	//Size range size to len+size
	struct skip_list_head *nh = rng->ranges.next[0];
	if (!nh)
		return;

	struct range *nrng = skip_list_entry(nh, struct range, ranges);
	assert(fequ(rng->start+rng->len, nrng->start));
	assert(fequ(s->now, rng->last_update));
	//Update next range

	rng->len = nrng->start-rng->start+nrng->len;

	rng->producer = nrng->producer;
	if (rng->producer)
		rng->producer->drng = rng;

	while(!list_empty(&nrng->consumers)){
		struct list_head *h = nrng->consumers.next;
		list_del(h);
		list_add(h, &rng->consumers);

		struct flow *f = skip_list_entry(h, struct flow, consumers);
		f->srng = rng;
	}

	skip_list_delete(nh);
	free(nrng);

	struct flow *f;
	_range_update(rng, s);
	list_for_each_entry(f, &rng->consumers, consumers) {
		_range_update(f->drng, s);
		range_calc_and_requeue_events(f, s);
	}

	if (rng->producer) {
		//Add to consumers
		_range_update(rng->producer->srng, s);
		range_calc_and_requeue_events(rng->producer, s);
	}
}

