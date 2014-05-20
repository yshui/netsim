#include "data.h"
#include "range.h"
#include "event.h"
#include "record.h"
#include "connect.h"

void user_lowwm_event(struct flow *f, struct sim_state *s){
	struct def_user *d = f->dst->user_data;
	struct user_event *ue;
	//Make sure the node state is updated!!
	assert(f->dst->state == d->next_state);
	range_update(f->drng, s);
	if (f->dst->state != N_PLAYING)
		return;

	int pos = d->buffer_pos;
	if (f->drng->start > pos)
		//Player hasn't reached this range yet
		return;

	if (!d->e) {
		ue = talloc(1, struct user_event);
		d->e = event_new(0, USER, ue);
	}
	event_remove(d->e);
	//Calculate event given the new range grow.
	//The range->grow should be updated by now, otherwise
	//you queued the user handler wrong.
	double br = d->bit_rate;
	if (f->drng->ranges.next[0] == NULL) {
		double time2 = (f->drng->total_len-pos)/br;
		double time3 = (f->drng->total_len-f->drng->start-f->drng->len)/
				f->bandwidth;
		if (time2 > time3) {
			//When finish downloading before reach end.
			ue = d->e->data;
			d->e->time = s->now+time2;
			ue->type = DONE_PLAY;
			ue->data = f;
			event_add(d->e, s);
			return;
		}
	}

	if (br < f->drng->grow) {
		//No extra events needed.
		//No done event because there's a next range.
		return;
	}

	//Stop playing after hit 10% buffer
	int limit = d->lowwm;
	double time = (f->drng->start+f->drng->len-pos-limit)/(br-f->drng->grow);
	assert(f->drng->start+f->drng->len > pos+limit);

	if ((f->done && time+s->now > f->done->time) ||
	    (f->drain && time+s->now > f->drain->time)) {
		//The range will "done" before we hit low water mark.
		//The low water mark event will be calculated when
		//handling next done event. And at that time buf_pos
		//is definitely still in this range.

		//Or

		//The range will hit a speed throttle or eof before low
		//water mark. The low water mark will be calculated at
		//that time
		return;
	}

	ue = d->e->data;
	d->e->time = time+s->now;
	ue->type = PAUSE_BUFFERING;
	ue->data = f;
	event_add(d->e, s);
}

void user_highwm_event(struct flow *f, struct sim_state *s){
	struct range *rng = f->drng;
	range_update(rng, s);
	struct node *n = f->dst;
	struct def_user *d = n->user_data;
	struct user_event *ue;
	//Make sure the node state is updated!!
	assert(n->state == d->next_state);
	if (n->state != N_STALE)
		//Isn't stale, already highwm
		return;
	struct skip_list_head *nh = rng->ranges.next[0];
	if (d->buffer_pos < rng->start) {
		//Not in this range
		return;
	}

	if (!d->e) {
		ue = talloc(1, struct user_event);
		d->e = event_new(0, USER, ue);
	}
	event_remove(d->e);

	int re = rng->start+rng->len-d->buffer_pos;
	double time = (d->highwm-re)/f->bandwidth;
	if (!nh) {
		//Reaching the eof count as highwm
		double time2 = (rng->total_len-rng->start-rng->len)/f->bandwidth;
		if (time2 < time)
			time = time2;
	}
	time += s->now;
	if ((!f->done || time < f->done->time) &&
	    (!f->drain || time < f->drain->time)) {
		d->e->time = time;
		ue = d->e->data;
		ue->type = DONE_BUFFERING;
		ue->data = f;
		event_add(d->e, s);
	}
}

void user_recalculate_event2(struct def_user *d, struct sim_state *s){
	struct node *n = d->n;
	struct resource *rsrc = store_get(n->store, d->resource);
	struct range *rng = range_get(rsrc, d->buffer_pos);
	user_lowwm_event(rng->producer, s);
}

void user_speed_change(struct event *e, struct sim_state *s){
	//Recalculate event is enough
	struct spd_event *se = e->data;
	if (se->type != P_RCV)
		//receive speed not changed
		return;
	struct def_user *d = se->c->peer[1]->user_data;

	//update buffer_pos
	if (se->c->peer[1]->state == N_PLAYING)
		d->buffer_pos += d->bit_rate*(s->now-d->last_update)+eps;
	d->last_update = s->now;
	user_lowwm_event(se->c->f, s);
	user_highwm_event(se->c->f, s);
}

void user_done(struct event *e, struct sim_state *s){
	struct flow *f = e->data;
	struct range *rng = f->drng;
	struct def_user *d = f->dst->user_data;
	if (f->dst->state == N_PLAYING)
		d->buffer_pos += d->bit_rate*(s->now-d->last_update)+eps;
	d->last_update = s->now;
	if (rng->start+rng->len == rng->total_len){
		//Already hit the end of the resource
		d->next_state = N_DONE;
		return;
	}
	//The drng is merged with next range
	user_lowwm_event(e->data, s);
	user_highwm_event(e->data, s);
}

