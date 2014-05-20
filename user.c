#include "data.h"
#include "range.h"
#include "event.h"
#include "record.h"
#include "connect.h"
#include "user.h"

void user_lowwm_event(struct range *rng, struct sim_state *s){
	struct def_user *d = rng->owner->user_data;
	struct user_event *ue;
	//Make sure the node state is updated!!
	assert(d->n->state == d->next_state);
	range_update(rng, s);
	if (d->n->state != N_PLAYING)
		return;

	int pos = d->buffer_pos;
	if (rng->start > pos)
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
	if (rng->ranges.next[0] == NULL) {
		double time2 = (rng->total_len-pos)/br;
		double time3 = (rng->total_len-rng->start-rng->len)/
				rng->grow;
		if (rng->total_len == rng->start+rng->len)
			time3 = 0;
		if (time2 > time3) {
			//When finish downloading before reach end.
			ue = d->e->data;
			d->e->time = s->now+time2;
			ue->type = DONE_PLAY;
			ue->d = d;
			ue->data = NULL;
			event_add(d->e, s);
			return;
		}
	}

	if (br < rng->grow) {
		//No extra events needed.
		//No done event because there's a next range.
		return;
	}

	//Stop playing after hit 10% buffer
	int limit = d->lowwm;
	double time = (rng->start+rng->len-pos-limit)/(br-rng->grow);
	assert(rng->start+rng->len > pos+limit);

	if (rng->producer) {
		struct flow *f = rng->producer;
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
	}

	ue = d->e->data;
	d->e->time = time+s->now;
	ue->type = PAUSE_BUFFERING;
	ue->d = d;
	ue->data = rng;
	event_add(d->e, s);
}

void user_highwm_event(struct range *rng, struct sim_state *s){
	range_update(rng, s);
	struct node *n = rng->owner;
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
	double time = (d->highwm-re)/rng->grow;
	if (!nh) {
		//Reaching the eof count as highwm
		double time2 = (rng->total_len-rng->start-rng->len)/rng->grow;
		if (time2 < time)
			time = time2;
		if (time2-time > -eps && time2-time < eps) {
			//FLOW_DONE overlap with highwm
		}
	}
	time += s->now;
	struct flow *f = rng->producer;
	if (!f || ((!f->done || time < f->done->time) &&
	    (!f->drain || time < f->drain->time))) {
		d->e->time = time;
		ue = d->e->data;
		ue->type = DONE_BUFFERING;
		ue->data = rng;
		ue->d = d;
		event_add(d->e, s);
	}
}

void user_recalculate_event2(struct def_user *d, struct sim_state *s){
	struct node *n = d->n;
	struct resource *rsrc = store_get(n->store, d->resource);
	struct range *rng = range_get(rsrc, d->buffer_pos);
	user_lowwm_event(rng, s);
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
	user_lowwm_event(se->c->f->drng, s);
	user_highwm_event(se->c->f->drng, s);
}

void user_done(struct event *e, struct sim_state *s){
	struct flow *f = e->data;
	struct range *rng = f->drng;
	struct def_user *d = f->dst->user_data;
	if (f->dst->state == N_PLAYING)
		d->buffer_pos += d->bit_rate*(s->now-d->last_update)+eps;
	d->last_update = s->now;
	/*if (rng->start+rng->len == rng->total_len){
		//Already hit the end of the resource
		d->next_state = N_DONE;
		return;
	}*/
	//The drng is merged with next range
	user_lowwm_event(rng, s);
	user_highwm_event(rng, s);
}

