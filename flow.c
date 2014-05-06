#include <stdlib.h>
#include <assert.h>
#include "data.h"
#include "store.h"
#include "range.h"
#include "event.h"
#include "node.h"
#include "connect.h"

void flow_done_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	f->drng->len += f->bandwidth*(s->now-f->drng->last_update);
	f->drng->last_update = s->now;
	range_merge_with_next(f->drng);
	connection_close(f->c, P_RCV, s);
}

void flow_throttle_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	f->drng->len += f->bandwidth*(s->now-f->drng->last_update);
	f->drng->last_update = s->now;
	assert(f->bandwidth > f->srng->grow);
	bwspread(f->c, f->srng->grow-f->bandwidth, 0, P_SND, s);
	assert(f->bandwidth == f->srng->grow);

	//Queue a event to notify the dst this speed change
	queue_speed_event(f->c, P_RCV, 0, f->bandwidth, s);

	//Recalculate the drain and done event
	event_remove(f->done);
	event_remove(f->drain);
	range_calc_flow_events(f);
}
