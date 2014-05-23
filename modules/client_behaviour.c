#define LOG_DOMAIN "client_behaviour"

#include "sim.h"
#include "flow.h"
#include "store.h"
#include "range.h"
#include "user.h"
#include "skiplist.h"
#include "list.h"
#include "record.h"
#include "p2p_common.h"

#include "common.h"

#include <assert.h>

void client_lowwm_event(struct range *rng, struct sim_state *s){
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
	double sgrow = rng->producer ? rng->producer->speed[1] : 0;
	if (rng->ranges.next[0] == NULL) {
		double time2 = (rng->total_len-pos)/br;
		double time3 = (rng->total_len-rng->start-rng->len)/sgrow;
		if (fequ(rng->total_len, rng->start+rng->len))
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

	if (br < sgrow) {
		//No extra events needed.
		//No done event because there's a next range.
		return;
	}

	//Stop playing after hit lowwm
	int limit = d->lowwm;
	double time = (rng->start+rng->len-pos-limit)/(br-sgrow);
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

void client_highwm_event(struct range *rng, struct sim_state *s){
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
	double sgrow = rng->producer ? rng->producer->speed[1] : 0;
	double time = (d->highwm-re)/sgrow;
	if (re >= d->highwm)
		time = 0;
	if (!nh) {
		//Reaching the eof count as highwm
		double time2 = (rng->total_len-rng->start-rng->len)/sgrow;
		if (fequ(rng->total_len, rng->start+rng->len))
			time2 = 0;
		if (time2 < time)
			time = time2;
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



void client_next_state_from_event(struct event *e, struct sim_state *s){
	struct user_event *ue = e->data;
	if (ue->type == DONE_PLAY) {
		//The connection might already be closed.
		struct def_user *d = ue->d;
		d->next_state = N_DONE;
		return;
	}
	struct flow *f = ue->data;
	struct def_user *d = ue->d;
	struct node *n = d->n;
	d->trigger = ue->data;
	switch (ue->type) {
		case PAUSE_BUFFERING:
			//Calculate event for highwm
			d->next_state = N_STALE;
			break;
		case DONE_BUFFERING:
			d->next_state = N_PLAYING;
			break;
		case DONE_PLAY:
			//Queue next event
			d->next_state = N_DONE;
			break;
		default:
			break;
	}
}

void client_handle_next_state(struct node *n, struct sim_state *s){
	struct def_user *d = n->user_data;
	struct range *rng = d->trigger;
	if (n->state == d->next_state)
		return;
	int o_state = n->state;
	n->state = d->next_state;
	switch(o_state){
		case N_PLAYING:
			d->buffer_pos += d->bit_rate*(s->now-d->last_update)+eps;
			d->last_update = s->now;
			if (d->next_state == N_STALE)
				client_highwm_event(rng, s);
			break;
		case N_STALE:
			//Not playing, buffer_pos unchanged
			d->last_update = s->now;
			if (d->next_state == N_PLAYING)
				client_lowwm_event(rng, s);
			break;
		default:
			assert(false);
	}
}

int client_new_connection(id_t rid, size_t start, struct node *server,
			   struct node *client, struct sim_state *s){
	struct flow *f = sim_establish_flow(rid, start, server, client, s);

	if (!f)
		return -1;

	resource_add_provider(f->resource_id, client, s);
	if (client->state != N_PLAYING && client->state != N_STALE)
		//state == N_DONE, N_IDLE, N_OFFLINE
		return 0;

	//update previous range's user events
	struct skip_list_head *ph = f->drng->ranges.prev[0];
	struct range *prng = skip_list_entry(ph, struct range, ranges);
	client_lowwm_event(prng, s);
	client_highwm_event(prng, s);

	//Add resource to holders
	return 0;
}

int client_recalc_state(struct node *client){
	struct def_user *d = client->user_data;
	struct resource *r = store_get(client->store, d->resource);
	struct range *rng = range_get(r, d->buffer_pos);

	if (!rng)
		//nothing downloaded
		return N_STALE;

	if (d->buffer_pos == rng->total_len)
		return N_DONE;

	if (rng->start+rng->len == rng->total_len)
		return N_PLAYING;

	if (rng->start+rng->len > d->buffer_pos+d->lowwm)
		return N_PLAYING;

	return N_STALE;
}

void client_start_play(struct node *client, id_t rid, struct sim_state *s){
	struct def_user *d = client->user_data;
	struct resource *r = store_get(client->store, rid);
	struct range *rng = range_get(r, 0);
	d->resource = rid;
	d->buffer_pos = 0;
	d->last_update = s->now;
	d->bit_rate = r->bit_rate;
	d->resource = rid;
	d->next_state = client->state = client_recalc_state(client);

	if (!rng) {
		//Nothing downloaded yet
		assert(r->ranges.next[0]);
		rng = skip_list_entry(r->ranges.next[0], struct range, ranges);
	}
	client_lowwm_event(rng, s);
	client_highwm_event(rng, s);
}


void client_speed_change(struct event *e, struct sim_state *s){
	//Recalculate event is enough
	struct spd_event *se = e->data;
	if (se->type != P_RCV)
		//receive speed not changed
		return;
	struct def_user *d = se->f->peer[1]->user_data;

	//update buffer_pos
	if (se->f->peer[1]->state == N_PLAYING)
		d->buffer_pos += d->bit_rate*(s->now-d->last_update)+eps;
	d->last_update = s->now;
	client_lowwm_event(se->f->drng, s);
	client_highwm_event(se->f->drng, s);
}

void client_done(struct event *e, struct sim_state *s){
	struct flow *f = e->data;
	struct range *rng = f->drng;
	struct def_user *d = f->peer[1]->user_data;
	if (f->peer[1]->state == N_PLAYING)
		d->buffer_pos += d->bit_rate*(s->now-d->last_update)+eps;
	d->last_update = s->now;
	/*if (rng->start+rng->len == rng->total_len){
		//Already hit the end of the resource
		d->next_state = N_DONE;
		return;
	}*/
	//The drng is merged with next range
	client_lowwm_event(rng, s);
	client_highwm_event(rng, s);

}

void client_next_event(struct node *n, struct sim_state *s){
	struct def_sim *ds = s->user_data;
	struct def_user *d = n->user_data;
	int elapsed = s->now/60.0/60.0;
	int nowh = (ds->start_hour+elapsed+d->time_zone)%24;
	double time = get_break_by_hour(nowh);
	struct user_event *ue = talloc(1, struct user_event);
	ue->type = NEW_CONNECTION;
	ue->data = n;
	struct event *e = event_new(s->now+time, USER, ue);
	event_add(e, s);
}

static inline bool
is_resource_usable(struct resource *r, size_t start, struct sim_state *s){
	struct range *rng = range_get_by_start(r, start);
	range_update(rng, s);
	if (rng->start+rng->len <= start)
		return false;
	return true;
}

static inline bool
is_node_usable(struct node *n, id_t rid, size_t start, struct sim_state *s){
	struct resource *r = store_get(n->store, rid);
	assert(r->owner == n);
	if (!r)
		return false;
	return is_resource_usable(r, start, s);
}

struct node *
server_picker1(id_t rid, size_t start, struct node *client, struct sim_state *s){
	//Only choose the servers
	struct def_sim *ds = s->user_data;
	struct server *ss = NULL;
	bool found = false;
	list_for_each_entry(ss, &ds->servers, servers)
		if (is_node_usable(ss->n, rid, start ,s) &&
		    !is_connected(ss->n, client))
			return ss->n;
	return NULL;
}

struct node *
server_picker2(id_t rid, size_t start, struct node *client, struct sim_state *s){
	//Choose any non-server nodes
	struct def_sim *ds = s->user_data;
	struct resource_entry *re = NULL;
	HASH_FIND_INT(ds->rsrcs, &rid, re);
	if (!re)
		return NULL;

	struct resource_provider *rp, *tmp;
	HASH_ITER(hh, re->holders, rp, tmp)
		if (is_resource_usable(rp->r, start, s) &&
		    !is_connected(rp->r->owner, client))
			return rp->r->owner;
	return NULL;
}
