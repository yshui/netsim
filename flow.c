#include <stdlib.h>
#include "data.h"
#include "store.h"
#include "range.h"
#include "event.h"
#include "node.h"

void flow_done_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	f->drng->len += f->bandwidth*(s->now-f->drng->last_update);
	f->drng->last_update = s->now;
	range_merge_with_next(f->drng);
}

void flow_throttle_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	f->drng->len += f->bandwidth*(s->now-f->drng->last_update);
	f->drng->last_update = s->now;
	f->bandwidth = f->srng->grow;
	bwspread
	event_remove(f->done);
	event_remove(f->drain);
	range_calc_flow_events(f);
}
