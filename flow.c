#include <stdlib.h>
#include <assert.h>

#define LOG_DOMAIN "flow"

#include "common.h"
#include "flow.h"
#include "data.h"
#include "store.h"
#include "range.h"
#include "event.h"
#include "record.h"

extern inline void
queue_speed_event(struct flow *f, int dir,
		  double speed, struct sim_state *s){
	struct spd_event *se = talloc(1, struct spd_event);
	se->type = dir;
	se->speed = speed;
	se->e = event_new(s->now+f->delay, SPEED_CHANGE, se);
	se->f = f;

	list_add(&se->spd_evs, &f->spd_evs);

	event_add(se->e, s);
}

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

static inline double get_share(struct flow *f, int dir){
	struct node *n = f->peer[dir];
	double total = n->total_bwupbound[dir], max = n->maximum_bandwidth[dir];
	double used = n->bandwidth_usage[dir];
	return total > max ? f->bwupbound*max/total : f->bwupbound;
}

//Return the actual amount changed.
double bwspread(struct flow *f, double amount, int dir,
			      int close, struct sim_state *s){
	//Negative amount is always fulfilled (assuming the amount is sane, i.e.
	//-amount < c->speed[dir]), while positive amount is not.

	//Speed change is almost always due to the other end's speed is changed,
	//with the exception of connection closing and opening.

	//So I don't queue event to notify the other end of speed change, since
	//they should already know, I queue event only when the speed change
	//can't be fulfilled.

	//TODO Allow node to set arbitrary share for a connection.
	log_info("[%.06lf] bwspread start, f (%d->%d), amount %lf, dir %d, close %d\n",
		 s->now, f->peer[0]->node_id, f->peer[1]->node_id, amount, dir, close);

	if (close) {
		amount = -f->speed[dir];
		log_info("Closing, New amount: %lf\n", amount);
	}

	struct node *n = f->peer[dir];
	assert(!list_empty(&n->conns[dir]));
	struct list_head *h = &n->conns[dir];
	double total = n->total_bwupbound[dir], max = n->maximum_bandwidth[dir];
	double used = n->bandwidth_usage[dir];
	double share = total > max ? f->bwupbound*max/total : f->bwupbound;
	double ret = 0;
	/* Increase/Decrease the bandwidth of c */
	if (amount > eps && f->speed[dir]+amount > share) {
		//amount > eps ---> close == 0
		if (f->speed[dir] > share) {
			//Trying to increase a connection's speed which
			//already exceeded it share.
			//Event is queued to notify the other end the change
			//is not possible
			log_info("Connection's speed already exceeds its share, bwspread stop\n");
			queue_speed_event(f, !dir, f->speed[dir], s);
			return 0;
		}
		queue_speed_event(f, !dir, share, s);
		amount = share-f->speed[dir];
		log_info("Share exceeded, New amount: %lf\n", amount);
	}

	if (amount > -eps && amount < eps) {
		log_info("amount = %lf, nothing to do.\n", amount);
		return 0;
	}

	f->speed[dir] += amount;
	/* Special Cases */
	if (total < max+eps) {
		if (!close || total+f->bwupbound < max+eps) {
			log_info("total(%lf) <= max(%lf), bwspread stop\n",
				 !close ? total : total+f->bwupbound, max);
			//The total bwupbound is & was lesser than the peer's
			//maximum bandwidth, every connection is at its upbound,
			//and not limited by their share, so there's no need to
			//spread.
			f->peer[dir]->bandwidth_usage[dir] += amount;
			return amount;
		}
		log_info("total(%lf) <= max(%lf), but closing, and total+"
			 "bwupbound(%lf) > max, bwspread continue\n", total,
			 max, total+f->bwupbound);
	}

	if (amount > -eps && used+amount < max+eps) {
		log_info("amount(%lf) > 0, used+amount(%lf) < max, bwspread stop\n", amount, used+amount);
		//There're enough free bandwidth
		n->bandwidth_usage[dir] += amount;
		return amount;
	}
	if (used < max) {
		log_info("used(%lf) < max(%lf)\n", used, max);
		n->bandwidth_usage[dir] = n->maximum_bandwidth[dir];
	}

	/* Gather/Spread the amount needed */
	struct flow *nf;
	double spread_amount = amount;
	if (amount > eps && used < max)
		spread_amount = amount-max+used;
	double e = 0;
	list_for_each_entry(nf, h, conns[dir]){
		if (nf == f)
			continue;
		//Don't increase speed of a closing connection
		if (nf->closing && amount < eps)
			continue;
		//lshare = the potential upbound of the connection
		//       = share of connection on the other end)
		//	   when amount < 0 (so the speed is going to increase);
		//
		//	 = share of connection on this end
		//	   when amount > 0;
		double lshare = nf->bwupbound*max/total;
		if (nf->closing)
			lshare = 0;
		if (amount < eps) {
			lshare = get_share(nf, !dir);
			if (nf->speed[dir] < lshare)
				e += lshare - nf->speed[dir];
		} else if (amount > eps && nf->speed[dir] > lshare)
			e += nf->speed[dir]-lshare;
	}

	log_info("e = %lf\n", e);

	if (amount < eps && -amount > e) {
		//Even we increase other connections' speed to their limit,
		//there are still free speed left.
		log_info("amount(%lf) < 0, e < -amount, new amount: %lf\n", amount, -e);
		n->bandwidth_usage[dir] += amount+e;
		amount = -e;
	}
	list_for_each_entry(nf, h, conns[dir]){
		if (nf == f)
			continue;
		if (nf->closing && amount < eps) {
			log_info("Connection %d closing\n", nf->flow_id);
			continue;
		}
		double lshare = nf->bwupbound*max/total;
		double delta;
		if (nf->closing)
			lshare = 0;
		if (amount < eps) {
			lshare = get_share(nf, !dir);
			if (nf->speed[dir] < lshare) {
				delta = lshare - nf->speed[dir];
				log_info("Connection (%d->%d) share: %lf, now: %lf, target: %lf\n",
					 nf->peer[0]->node_id, nf->peer[1]->node_id, lshare,
					 nf->speed[dir], nf->speed[dir]-amount*delta/e);
				double new_speed = nf->speed[dir]-amount*delta/e;
				if (dir == P_SND)
					//The rcv speed can't increase by itself.
					nf->speed[dir] -= new_speed;
				else
					log_info("dir == P_RCV, don't increase speed, notify the other end only.\n");
				//queue speed increase event to the other end
				queue_speed_event(nf, !dir, new_speed, s);
				//Update ranges
				range_calc_and_queue_event(nf->f, s);
			}else
				log_info("Connection (%d->%d) share: %lf, now: %lf, not changing\n",
					 nf->peer[0]->node_id, nf->peer[1]->node_id, lshare, nf->speed[dir]);
		} else if (amount > eps) {
			if (nf->speed[dir] > lshare) {
				delta = nf->speed[dir]-lshare;
				log_info("Connection (%d->%d) share: %lf, now: %lf, target: %lf\n",
					 nf->peer[0]->node_id, nf->peer[1]->node_id, lshare, nf->speed[dir], nf->speed[dir]-spread_amount*delta/e);
				nf->speed[dir] -= spread_amount*delta/e;
				//queue speed decrease event to the other end
				queue_speed_event(nf, !dir, nf->speed[dir], s);
				//e > spread_amount is impossible, otherwise this
				//connection's speed would exceed its share.
				//Update ranges
				range_calc_and_queue_event(nf->f, s);
			}else
				log_info("Connection (%d->%d) share: %lf, now: %lf, not changing\n",
					 nf->peer[0]->node_id, nf->peer[1]->node_id, lshare, nf->speed[dir]);
		}
	}
	log_info("bwspread done\n");
	return spread_amount;
}

//outbound/src/snd = [0], inbound/dst/rcv = [1]
//dir = who initiate the close, 0 = the src, 1 = the dst
//Close connection in both direction
void flow_close(struct flow *f, struct sim_state *s){
	//Spread the bandwidth to remaining connections.
	f->peer[0]->total_bwupbound[0] -= f->bwupbound;
	f->peer[1]->total_bwupbound[1] -= f->bwupbound;
	bwspread(f, f->speed[0], 0, 1, s);
	bwspread(f, f->speed[1], 1, 1, s);
	list_del(&f->conns[0]);
	list_del(&f->conns[1]);
	log_info("Remove flow %d %p\n", f->flow_id, f);
	HASH_DEL(s->flows, f);

	//Remove the corresponding flow from consumer
	struct list_head *h = &f->spd_evs;
	//Unqueue all of its events.
	while(!list_empty(h)){
		struct spd_event *tse =
			list_first_entry(h, struct spd_event, spd_evs);
		//list_del(h->next);
		list_del(&tse->spd_evs);
		event_remove(tse->e);
		free(tse->e);
		free(tse);
	}

	//Close the flow
	if (f->drng && f->drng->producer == f)
		f->drng->producer = NULL;
	list_del(&f->consumers);
	event_remove(f->done);
	event_remove(f->drain);
	event_free(f->done);
	event_free(f->drain);

	//Log connection close
	uint8_t type = 0;
	write_record(0, R_CONN_CLOSE, f->flow_id, 1, &type, s);
	free(f);
}

//outbound/src/snd = [0], inbound/dst/rcv = [1]
//connection creation is always initiated by src
struct flow *flow_create(struct node *src, struct node *dst,
				     struct sim_state *s){
	struct flow *f = talloc(1, struct flow);
	f->bwupbound =
		s->bwcalc(src->user_data, dst->user_data);
	f->peer[0] = src;
	f->peer[1] = dst;
	f->speed[1] = 0;
	f->delay = s->dlycalc(src->user_data, dst->user_data);
	f->closing = 0;
	INIT_LIST_HEAD(&f->spd_evs);
	list_add(&f->conns[0], &src->conns[0]);
	list_add(&f->conns[1], &dst->conns[1]);
	src->total_bwupbound[0] += f->bwupbound;
	dst->total_bwupbound[1] += f->bwupbound;

	double spd0 = get_share(f, 0);
	bwspread(f, spd0, 0, 0, s);

	queue_speed_event(f, P_RCV, f->speed[0], s);

	//Insert into connection hash
	id_t rand = random();
	struct flow *of = NULL;
	do {
		HASH_FIND_INT(s->flows, &rand, of);
	}while(of);
	f->flow_id = rand;
	HASH_ADD_INT(s->flows, flow_id, f);

	//Log connection creation
	write_record(0, R_CONN_CREATE, src->node_id, 4, &f->flow_id, s);
	write_record(0, R_CONN_DST, f->flow_id, 4, &dst->node_id, s);

	return f;
}

#define abs(x) ((x)>0?(x):-(x))

void handle_speed_change(struct event *e, struct sim_state *s){
	struct spd_event *se = e->data;
	struct flow *f = se->f;

	//Ignore events that are queued before our last event can reach the
	//other end.

	//By doing this, pending event will not be overwritten before it is
	//handled.
	//if (e->qtime < se->c->pending_event[se->type])
	//	return;

	if (se->type == P_RCV) {
		//Update the range before the speed is changed
		range_update(f->drng, s);
		range_update(f->srng, s);
	}

	double delta = se->speed-f->speed[se->type];

	bwspread(f, delta, se->type, 0, s);

	list_del(&se->spd_evs);

	if (se->type == P_RCV) {
		//Update the flow and its drng
		range_calc_and_queue_event(f, s);
		range_update_consumer_events(f->drng, s);
	} else if (delta > 0) {
		//SND speed increased, notify the RCV end
		queue_speed_event(f, P_RCV, f->speed[0], s);
	}


	//Log bandwidth usage
	struct node *n = f->peer[se->type];
	write_record(0, R_USAGE|se->type, n->node_id, -1,
		     &n->bandwidth_usage[se->type], s);

	//Log speed change
	write_record(0, R_SPD|se->type, f->flow_id, -1, &se->speed, s);
}

void speed_change_free(struct event *e, struct sim_state *s){
	struct spd_event *se = e->data;
	free(se);
}

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
	flow_close(f, s);
}

void flow_throttle_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	f->drain = NULL;
	range_update(f->drng, s);
	//If f->srng->producer == NULL, this should be a FLOW_DONE event
	assert(f->srng->producer);
	double delta = f->srng->producer->speed[1]-f->speed[0];
	assert(delta < 0);
	bwspread(f, delta, 0, P_SND, s);
	assert(fequ(f->speed[0], f->srng->producer->speed[1]));

	//Queue a event to notify the dst this speed change
	queue_speed_event(f, P_RCV, f->speed[0], s);

	//Recalculate the drain and done event
	range_calc_and_queue_event(f, s);
}
