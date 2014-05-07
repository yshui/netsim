#include "data.h"
#include "range.h"
#include "event.h"
#include "record.h"
#include "connect.h"

static void recalculate_user_events(struct spd_event *se, struct sim_state *s){
	if (!se->c) {
		//The connection is closed, so don't do anything
		return;
	}
	struct flow *f = se->c->f;
	struct range *rng = f->srng;
	struct def_user *d = f->dst->user_data;
	d->next_state = f->dst->state;
	if (rng->ranges.next[0] == NULL) {
		//Play finished
		d->next_state = N_DONE;
		return;
	}
	//Calculate event given the new range grow.
	//The range->grow should be updated by now, otherwise
	//you queued the user handler wrong.
	double br = d->bit_rate;
	if (br < f->drng->grow)
		//No extra event needed.
		return;

	int pos = d->buffer_pos;
	if (f->drng->start > pos)
		//Player hasn't reached this range yet
		return;

	//Stop playing after hit 10% buffer
	int limit = d->lowwm;
	double time = (pos-f->drng->start-limit)/(br-f->drng->grow);
	if (f->done && time+s->now > f->done->time)
		//The range will "done" before we hit low water mark.
		//The low water mark event will be calculated when
		//handling next done event.
		return;
	if (f->drain && time+s->now > f->drain->time)
		//The range will hit a speed throttle or eof before low
		//water mark. The low water mark will be calculated at
		//that time
		return;

	if (d->e) {
		event_remove(d->e);
		d->e->time = time+s->now;
	}else
		d->e = event_new(time+s->now, USER, f);

	event_add(d->e, s);
}

void user_speed_change(struct event *e, struct sim_state *s){
	//Recalculate event is enough
	recalculate_user_events(e->data, s);
}

void user_done(struct event *e, struct sim_state *s){
	struct flow *f = e->data;
	struct range *rng = f->drng;
	struct def_user *d = f->dst->user_data;
	if (rng->start+rng->len == rng->total_len){
		//Already hit the end of the resource
		d->next_state = N_DONE;
		return;
	}
	//The drng is merged with next range
	recalculate_user_events(e->data, s);
}

