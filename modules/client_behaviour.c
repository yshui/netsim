#include "sim.h"
#include "connect.h"
#include "store.h"
#include "range.h"
#include "user.h"
#include "skiplist.h"
#include "record.h"

#include "common.h"

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
				user_highwm_event(rng, s);
			break;
		case N_STALE:
			//Not playing, buffer_pos unchanged
			d->last_update = s->now;
			if (d->next_state == N_PLAYING)
				user_lowwm_event(rng, s);
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

	if (client->state != N_PLAYING && client->state != N_STALE)
		//state == N_DONE, N_IDLE, N_OFFLINE
		return 0;

	//update previous range's user events
	struct skip_list_head *ph = f->drng->ranges.prev[0];
	struct range *prng = skip_list_entry(ph, struct range, ranges);
	user_lowwm_event(prng, s);
	user_highwm_event(prng, s);

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
	user_lowwm_event(rng, s);
	user_highwm_event(rng, s);
}
