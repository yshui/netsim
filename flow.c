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
#include "record_wrapper.h"
#include "sim.h"

extern inline void
queue_speed_event(struct flow *f, int dir,
		  double speed, struct sim_state *s){
	struct spd_event *se = talloc(1, struct spd_event);
	se->type = dir;
	se->speed = speed;
	se->e = event_new(s->now+f->delay, SPEED_CHANGE, se);
	se->e->auto_free = true;
	se->f = f;

	list_add(&se->spd_evs, &f->spd_evs);

	event_add(se->e, s);
}

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

//Return the actual amount changed.
static inline void bwspread(struct flow *f, double amount, int dir,
			      int close, struct sim_state *s){
	//Negative amount is always fulfilled (assuming the amount is sane, i.e.
	//-amount < c->speed[dir]), while positive amount is not.

	//Speed change is almost always due to the other end's speed is changed,
	//with the exception of connection closing and opening.

	//So I don't queue event to notify the other end of speed change, since
	//they should already know, I queue event only when the speed change
	//can't be fulfilled.

	//TODO Allow node to set arbitrary share for a connection.
	log_debug("[%.06lf] bwspread start, f (%d->%d), amount %lf, dir %d, close %d\n",
		 s->now, f->peer[0]->node_id, f->peer[1]->node_id, amount, dir, close);

	if (close) {
		amount = -f->speed[dir];
		log_debug("Closing, New amount: %lf\n", amount);
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
			log_debug("Connection's speed already exceeds its share, bwspread stop\n");
			queue_speed_event(f, !dir, f->speed[dir], s);
			return;
		}
		queue_speed_event(f, !dir, share, s);
		amount = share-f->speed[dir];
		log_debug("Share exceeded, New amount: %lf\n", amount);
	}

	if (amount > -eps && amount < eps) {
		log_debug("amount = %lf, nothing to do.\n", amount);
		return;
	}

	f->speed[dir] += amount;
	if (amount < eps && amount > -32) {
		n->bandwidth_usage[dir] += amount;
		write_usage(dir, n, s);
		//If the decrease amount is too small, we don't spread
		return;
	}

	/* Special Cases */
	if (total < max+eps) {
		if (!close || total+f->bwupbound < max+eps) {
			log_debug("total(%lf) <= max(%lf), bwspread stop\n",
				 !close ? total : total+f->bwupbound, max);
			//The total bwupbound is & was lesser than the peer's
			//maximum bandwidth, every connection is at its upbound,
			//and not limited by their share, so there's no need to
			//spread.
			n->bandwidth_usage[dir] += amount;
			write_usage(dir, n, s);
			return;
		}
		log_debug("total(%lf) <= max(%lf), but closing, and total+"
			 "bwupbound(%lf) > max, bwspread continue\n", total,
			 max, total+f->bwupbound);
	}

	//From this point on, bandwidth_usage will be recalculated
	double spread_amount = amount;
	if (amount > -eps) {
		if (used+amount < max+eps) {
			log_debug("amount(%lf) > 0, used+amount(%lf) < max, bwspread stop\n", amount, used+amount);
			//There're enough free bandwidth
			n->bandwidth_usage[dir] += amount;
			write_usage(dir, n, s);
			return;
		} else  {
			log_debug("used(%lf) < max(%lf)\n", used, max);
			spread_amount = amount-max+used;
		}
	}

	/* Gather/Spread the amount needed */
	struct flow *nf;
	double e = 0;
	list_for_each_entry(nf, h, conns[dir]){
		if (nf == f)
			continue;
		//lshare = the potential upbound of the connection
		//       = share of connection on the other end)
		//	   when amount < 0 (so the speed is going to increase);
		//
		//	 = share of connection on this end
		//	   when amount > 0;
		double lshare = nf->bwupbound*max/total;
		if (amount < eps) {
			lshare = get_share(nf, !dir);
			if (nf->speed[dir] < lshare)
				e += lshare - nf->speed[dir];
		} else if (amount > eps && nf->speed[dir] > lshare)
			e += nf->speed[dir]-lshare;
	}

	log_debug("e = %lf\n", e);

	if (amount < eps && -amount > e) {
		//Even if we increase other connections' speed to their limit,
		//there are still free speed left.
		log_debug("amount(%lf) < 0, e < -amount, new amount: %lf\n", amount, -e);
		amount = -e;
	}

	double new_use = 0;
	list_for_each_entry(nf, h, conns[dir]){
		if (nf == f) {
			new_use += f->speed[dir];
			continue;
		}
		double lshare = nf->bwupbound*max/total;
		double delta;
		if (amount < eps) {
			lshare = get_share(nf, !dir);
			if (nf->speed[dir] < lshare) {
				delta = lshare - nf->speed[dir];
				log_debug("Connection (%d->%d) share: %lf, now: %lf, target: %lf\n",
					 nf->peer[0]->node_id, nf->peer[1]->node_id, lshare,
					 nf->speed[dir], nf->speed[dir]-amount*delta/e);
				double new_speed = nf->speed[dir]-amount*delta/e;
				if (dir == P_SND) {
					//The rcv speed can't increase by itself.
					nf->speed[dir] = new_speed;
				} else
					log_debug("dir == P_RCV, don't increase speed, notify the other end only.\n");
				//queue speed increase event to the other end
				queue_speed_event(nf, !dir, new_speed, s);
			}else
				log_debug("Connection (%d->%d) share: %lf, now: %lf, not changing\n",
					 nf->peer[0]->node_id, nf->peer[1]->node_id, lshare, nf->speed[dir]);
		} else if (amount > eps) {
			if (nf->speed[dir] > lshare) {
				delta = nf->speed[dir]-lshare;
				log_debug("Connection (%d->%d) share: %lf, now: %lf, target: %lf\n",
					 nf->peer[0]->node_id, nf->peer[1]->node_id, lshare, nf->speed[dir], nf->speed[dir]-spread_amount*delta/e);
				nf->speed[dir] -= spread_amount*delta/e;
				//queue speed decrease event to the other end
				queue_speed_event(nf, !dir, nf->speed[dir], s);
				//e > spread_amount is impossible, otherwise this
				//connection's speed would exceed its share.
				//Update ranges
				if (dir == P_RCV)
					range_calc_and_requeue_events(nf, s);
			}else
				log_debug("Connection (%d->%d) share: %lf, now: %lf, not changing\n",
					 nf->peer[0]->node_id, nf->peer[1]->node_id, lshare, nf->speed[dir]);
		}
		new_use += nf->speed[dir];
	}
	n->bandwidth_usage[dir] = new_use;
	write_usage(dir, n, s);
	log_debug("bwspread done, new_use %lf\n", new_use);
	return;
}

#ifndef NDEBUG
//For debug only
static bool _conn_fsck(struct node *src){
	struct flow *tf;
	double cnt = 0;
	double tbw = 0;
	list_for_each_entry_reverse(tf, &src->conns[0], conns[0]){
		assert(tf->peer[0] == src);
		struct node *dst = tf->peer[1];
		struct flow *tdst = NULL;
		HASH_FIND(hh2, src->outs, &dst->node_id, sizeof(dst->node_id), tdst);
		assert(tf == tdst);
		cnt += tf->speed[0];
		tbw += tf->bwupbound;
	}
	assert(fequ(tbw, src->total_bwupbound[0]));
	assert(fequ(cnt, src->bandwidth_usage[0]));
	cnt = 0;
	tbw = 0;
	list_for_each_entry_reverse(tf, &src->conns[1], conns[1]){
		assert(tf->peer[1] == src);
		struct node *dst = tf->peer[0];
		cnt += tf->speed[1];
		tbw += tf->bwupbound;
	}
	assert(fequ(tbw, src->total_bwupbound[1]));
	assert(fequ(cnt, src->bandwidth_usage[1]));
	return true;
}
#endif

//outbound/src/snd = [0], inbound/dst/rcv = [1]
//dir = who initiate the close, 0 = the src, 1 = the dst
//Close connection in both direction
void flow_close(struct flow *f, struct sim_state *s){
	assert(_conn_fsck(f->peer[0]));
	assert(_conn_fsck(f->peer[1]));
	assert(is_connected(f->peer[0], f->peer[1]));
	//Spread the bandwidth to remaining connections.
	f->peer[0]->total_bwupbound[0] -= f->bwupbound;
	f->peer[1]->total_bwupbound[1] -= f->bwupbound;
	flow_range_update(f, s);
	bwspread(f, f->speed[0], 0, 1, s);
	bwspread(f, f->speed[1], 1, 1, s);
	list_del(&f->conns[0]);
	list_del(&f->conns[1]);
	assert(_conn_fsck(f->peer[0]));
	assert(_conn_fsck(f->peer[1]));
	log_debug("Remove flow %d %p\n", f->flow_id, f);
	HASH_DEL(s->flows, f);
	HASH_DELETE(hh2, f->peer[0]->outs, f);

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
	f->srng->owner->nconsumer--;
	f->drng->owner->nproducer--;
	assert(f->srng->owner->nconsumer >= 0);
	assert(f->drng->owner->nproducer >= 0);
	struct resource *sr = f->srng->owner;
	if (sr->nconsumer == 0 && sr->nproducer == 0 && sr->auto_delete)
		node_del_resource(f->srng->owner);
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
	assert(_conn_fsck(src));
	//Don't create multiple flow between same pair of nodes
	assert(!is_connected(src, dst));
	struct flow *f;
	f = talloc(1, struct flow);
	f->bwupbound =
		s->bwcalc(src->user_data, dst->user_data);
	f->peer[0] = src;
	f->peer[1] = dst;
	f->speed[1] = 0;
	f->dst_id = dst->node_id;
	f->delay = s->dlycalc(src->user_data, dst->user_data);
	INIT_LIST_HEAD(&f->spd_evs);
	list_add(&f->conns[0], &src->conns[0]);
	list_add(&f->conns[1], &dst->conns[1]);
	HASH_ADD(hh2, src->outs, dst_id, sizeof(f->dst_id), f);
	src->total_bwupbound[0] += f->bwupbound;
	dst->total_bwupbound[1] += f->bwupbound;

	double spd0 = get_share(f, 0);
	bwspread(f, spd0, 0, 0, s);
	assert(_conn_fsck(f->peer[0]));
	assert(_conn_fsck(f->peer[1]));

	queue_speed_event(f, P_RCV, f->speed[0], s);

	//Insert into connection hash
	struct flow *of = NULL;
	do {
		f->flow_id = random();
		HASH_FIND_INT(s->flows, &rand, of);
	}while(of);
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
	assert(_conn_fsck(f->peer[0]));
	assert(_conn_fsck(f->peer[1]));

	//Ignore events that are queued before our last event can reach the
	//other end.

	//By doing this, pending event will not be overwritten before it is
	//handled.
	//if (e->qtime < se->c->pending_event[se->type])
	//	return;

	if (se->type == P_RCV)
		//Update the range before the speed is changed
		flow_range_update(f, s);

	double delta = se->speed-f->speed[se->type];

	bwspread(f, delta, se->type, 0, s);
	assert(_conn_fsck(f->peer[0]));
	assert(_conn_fsck(f->peer[1]));

	list_del(&se->spd_evs);

	if (se->type == P_RCV) {
		//Update the flow and its drng
		range_calc_and_requeue_events(f, s);
		range_update_consumer_events(f->drng, s);
	} else if (delta > 0) {
		//SND speed increased, notify the RCV end
		queue_speed_event(f, P_RCV, f->speed[0], s);
	}


	//Log bandwidth usage
	//struct node *n = f->peer[se->type];
	//write_record(0, R_USAGE|se->type, n->node_id, -1,
	//	     &n->bandwidth_usage[se->type], s);

	//Log speed change
	write_record(0, R_SPD|se->type, f->flow_id, -1, &se->speed, s);
}

void speed_change_free(struct event *e, struct sim_state *s){
	struct spd_event *se = e->data;
	free(se);
}

void flow_done_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	assert(_conn_fsck(f->peer[0]));
	flow_range_update(f, s);
	struct skip_list_head *next = f->drng->ranges.next[0];
	if (next) {
		struct range *nrng = skip_list_entry(next, struct range, ranges);
		flow_range_update(nrng->producer, s);
		range_merge_with_next(f->drng, s);
	}
}

void flow_done_cleaner(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	flow_close(f, s);
}

void flow_throttle_handler(struct event *e, struct sim_state *s){
	struct flow *f = (struct flow *)e->data;
	//If f->srng->producer == NULL, this should be a FLOW_DONE event
	assert(f->srng->producer);
	double delta = f->srng->producer->speed[1]-f->speed[0];
	assert(delta < eps);
	flow_range_update(f, s);
	//Speed throttle don't have delay, this is a hack
	bwspread(f, delta, P_SND, 0, s);
	delta = f->srng->producer->speed[1]-f->speed[1];
	bwspread(f, delta, P_RCV, 0, s);
	range_calc_and_requeue_events(f, s);
	range_update_consumer_events(f->drng, s);
}
