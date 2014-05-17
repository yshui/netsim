#include <stdlib.h>
#include <assert.h>
#include "data.h"
#include "store.h"
#include "range.h"
#include "event.h"
#include "connect.h"

void flow_done_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	f->done = NULL;
	range_update(f->drng, s);
	struct skip_list_head *next = f->drng->ranges.next[0];
	if (next) {
		struct range *nrng = skip_list_entry(next, struct range, ranges);
		range_update(nrng, s);
		range_merge_with_next(f->drng, s);
	}
}

void flow_done_cleaner(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	connection_close(f->c, s);
}

void flow_throttle_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	f->drain = NULL;
	range_update(f->drng, s);
	assert(f->bandwidth > f->srng->grow);
	bwspread(f->c, f->srng->grow-f->bandwidth, 0, P_SND, s);
	assert(f->bandwidth == f->srng->grow);

	//Queue a event to notify the dst this speed change
	queue_speed_event(f->c, P_RCV, f->bandwidth, s);

	//Recalculate the drain and done event
	range_calc_and_queue_event(f, s);
}
