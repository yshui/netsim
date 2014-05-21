#include <assert.h>

#define LOG_DOMAIN "connect"

#include "log.h"
#include "connect.h"
#include "event.h"
#include "range.h"
#include "record.h"

extern inline void
queue_speed_event(struct connection *c, int dir,
		  double speed, struct sim_state *s){
	struct spd_event *se = talloc(1, struct spd_event);
	se->type = dir;
	se->speed = speed;
	se->e = event_new(s->now+c->delay, SPEED_CHANGE, se);
	se->c = c;

	list_add(&se->spd_evs, &c->spd_evs);

	event_add(se->e, s);

	c->pending_event[!dir] = s->now+c->delay;
}

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

static inline double get_share(struct connection *c, int dir){
	struct node *n = c->peer[dir];
	double total = n->total_bwupbound[dir], max = n->maximum_bandwidth[dir];
	double used = n->bandwidth_usage[dir];
	return total > max ? c->bwupbound*max/total : c->bwupbound;
}

//Return the actual amount changed.
double bwspread(struct connection *c, double amount, int dir,
			      int close, struct sim_state *s){
	//Negative amount is always fulfilled (assuming the amount is sane, i.e.
	//-amount < c->speed[dir]), while positive amount is not.

	//Speed change is almost always due to the other end's speed is changed,
	//with the exception of connection closing and opening.

	//So I don't queue event to notify the other end of speed change, since
	//they should already know, I queue event only when the speed change
	//can't be fulfilled.

	//TODO Allow node to set arbitrary share for a connection.
	log_info("[%.06lf] bwspread start, c (%d->%d), amount %lf, dir %d, close %d\n",
		 s->now, c->peer[0]->node_id, c->peer[1]->node_id, amount, dir, close);

	if (close) {
		amount = -c->speed[dir];
		log_info("Closing, New amount: %lf\n", amount);
	}

	struct node *n = c->peer[dir];
	assert(!list_empty(&n->conns[dir]));
	struct list_head *h = &n->conns[dir];
	double total = n->total_bwupbound[dir], max = n->maximum_bandwidth[dir];
	double used = n->bandwidth_usage[dir];
	double share = total > max ? c->bwupbound*max/total : c->bwupbound;
	double ret = 0;
	/* Increase/Decrease the bandwidth of c */
	if (amount > eps && c->speed[dir]+amount > share) {
		//amount > eps ---> close == 0
		if (c->speed[dir] > share) {
			//Trying to increase a connection's speed which
			//already exceeded it share.
			//Event is queued to notify the other end the change
			//is not possible
			log_info("Connection's speed already exceeds its share, bwspread stop\n");
			queue_speed_event(c, !dir, c->speed[dir], s);
			return 0;
		}
		queue_speed_event(c, !dir, share, s);
		amount = share-c->speed[dir];
		log_info("Share exceeded, New amount: %lf\n", amount);
	}

	if (amount > -eps && amount < eps) {
		log_info("amount = %lf, nothing to do.\n", amount);
		return 0;
	}

	c->speed[dir] += amount;
	/* Special Cases */
	if (total < max+eps) {
		if (!close || total+c->bwupbound < max+eps) {
			log_info("total(%lf) < max(%lf), bwspread stop\n",
				 close ? total : total+c->bwupbound, max);
			//The total bwupbound is & was lesser than the peer's
			//maximum bandwidth, every connection is at its upbound,
			//and not limited by their share, so there's no need to
			//spread.
			c->peer[dir]->bandwidth_usage[dir] += amount;
			return amount;
		}
		log_info("total(%lf) < max(%lf), but closing, and total+"
			 "bwupbound(%lf) > max, bwspread continue\n", total,
			 max, total+c->bwupbound);
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
	struct connection *nc;
	double spread_amount = amount;
	if (amount > eps && used < max)
		spread_amount = amount-max+used;
	double e = 0;
	list_for_each_entry(nc, h, conns[dir]){
		if (nc == c)
			continue;
		//Don't increase speed of a closing connection
		if (nc->closing && amount < eps)
			continue;
		//lshare = the potential upbound of the connection
		//       = share of connection on the other end)
		//	   when amount < 0 (so the speed is going to increase);
		//
		//	 = share of connection on this end
		//	   when amount > 0;
		double lshare = nc->bwupbound*max/total;
		if (nc->closing)
			lshare = 0;
		if (amount < eps) {
			lshare = get_share(nc, !dir);
			if (nc->speed[dir] < lshare)
				e += lshare - nc->speed[dir];
		} else if (amount > eps && nc->speed[dir] > lshare)
			e += nc->speed[dir]-lshare;
	}

	log_info("e = %lf\n", e);

	if (amount < eps && -amount > e) {
		//Even we increase other connections' speed to their limit,
		//there are still free speed left.
		log_info("amount(%lf) < 0, e < -amount, new amount: %lf\n", amount, -e);
		n->bandwidth_usage[dir] += amount+e;
		amount = -e;
	}
	list_for_each_entry(nc, h, conns[dir]){
		if (nc == c)
			continue;
		if (nc->closing && amount < eps) {
			log_info("Connection %d closing\n", nc->conn_id);
			continue;
		}
		double lshare = nc->bwupbound*max/total;
		double delta;
		if (nc->closing)
			lshare = 0;
		if (amount < eps) {
			lshare = get_share(nc, !dir);
			if (nc->speed[dir] < lshare) {
				delta = lshare - nc->speed[dir];
				log_info("Connection (%d->%d) share: %lf, now: %lf, target: %lf\n",
					 nc->peer[0]->node_id, nc->peer[1]->node_id, lshare,
					 nc->speed[dir], nc->speed[dir]-amount*delta/e);
				nc->speed[dir] -= amount*delta/e;
				//queue speed increase event to the other end
				queue_speed_event(nc, !dir, nc->speed[dir], s);
			}else
				log_info("Connection (%d->%d) share: %lf, now: %lf, not changing\n",
					 nc->peer[0]->node_id, nc->peer[1]->node_id, lshare, nc->speed[dir]);
		} else if (amount > eps) {
			if (nc->speed[dir] > lshare) {
				delta = nc->speed[dir]-lshare;
				log_info("Connection (%d->%d) share: %lf, now: %lf, target: %lf\n",
					 nc->peer[0]->node_id, nc->peer[1]->node_id, lshare, nc->speed[dir], nc->speed[dir]-spread_amount*delta/e);
				nc->speed[dir] -= spread_amount*delta/e;
				//queue speed decrease event to the other end
				queue_speed_event(nc, !dir, nc->speed[dir], s);
				//e > spread_amount is impossible, otherwise this
				//connection's speed would exceed its share.
			}else
				log_info("Connection (%d->%d) share: %lf, now: %lf, not changing\n",
					 nc->peer[0]->node_id, nc->peer[1]->node_id, lshare, nc->speed[dir]);
		}
	}
	log_info("bwspread done\n");
	return spread_amount;
}

//outbound/src/snd = [0], inbound/dst/rcv = [1]
//dir = who initiate the close, 0 = the src, 1 = the dst
//Close connection in both direction
void connection_close(struct connection *c, struct sim_state *s){
	//Spread the bandwidth to remaining connections.
	c->peer[0]->total_bwupbound[0] -= c->bwupbound;
	c->peer[1]->total_bwupbound[1] -= c->bwupbound;
	bwspread(c, c->speed[0], 0, 1, s);
	bwspread(c, c->speed[1], 1, 1, s);
	list_del(&c->conns[0]);
	list_del(&c->conns[1]);
	HASH_DEL(s->conns, c);

	//Remove the corresponding flow from consumer
	struct flow *f = c->f;
	struct list_head *h = &c->spd_evs;
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
	if (f->drng && f->drng->producer == f) {
		f->drng->grow = 0;
		f->drng->producer = NULL;
	}
	list_del(&f->consumers);
	event_remove(f->done);
	event_remove(f->drain);
	event_free(f->done);
	event_free(f->drain);
	HASH_DEL(s->flows, f);
	free(f);


	//Log connection close
	uint8_t type = 0;
	write_record(0, R_CONN_CLOSE, c->conn_id, 1, &type, s);
}

//outbound/src/snd = [0], inbound/dst/rcv = [1]
//connection creation is always initiated by src
struct connection *connection_create(struct node *src, struct node *dst,
				     struct sim_state *s){
	struct connection *c = talloc(1, struct connection);
	c->bwupbound =
		s->bwcalc(src->user_data, dst->user_data);
	c->peer[0] = src;
	c->peer[1] = dst;
	c->speed[1] = 0;
	c->delay = s->dlycalc(src->user_data, dst->user_data);
	c->closing = 0;
	INIT_LIST_HEAD(&c->spd_evs);
	list_add(&c->conns[0], &src->conns[0]);
	list_add(&c->conns[1], &dst->conns[1]);
	src->total_bwupbound[0] += c->bwupbound;
	dst->total_bwupbound[1] += c->bwupbound;

	double spd0 = get_share(c, 0);
	bwspread(c, spd0, 0, 0, s);

	queue_speed_event(c, P_RCV, c->speed[0], s);

	//Insert into connection hash
	id_t rand = random();
	struct connection *oc = NULL;
	do {
		HASH_FIND_INT(s->conns, &rand, oc);
	}while(oc);
	c->conn_id = rand;
	HASH_ADD_INT(s->conns, conn_id, c);

	//Log connection creation
	write_record(0, R_CONN_CREATE, src->node_id, 4, &c->conn_id, s);
	write_record(0, R_CONN_DST, c->conn_id, 4, &dst->node_id, s);

	return c;
}

#define abs(x) ((x)>0?(x):-(x))

void handle_speed_change(struct event *e, struct sim_state *s){
	struct spd_event *se = e->data;
	struct connection *c = se->c;
	struct flow *f = c->f;

	//Ignore events that are queued before our last event can reach the
	//other end.

	//By doing this, pending event will not be overwritten before it is
	//handled.
	//if (e->qtime < se->c->pending_event[se->type])
	//	return;

	bwspread(se->c, se->speed-se->c->speed[se->type], se->type, 0, s);
	//The pending event has been handled now.
	c->pending_event[!se->type] = 0;

	list_del(&se->spd_evs);

	if (se->type == P_RCV){
		//Update the flow and its drng
		range_update(f->drng, s);
		f->bandwidth = f->drng->grow = c->speed[1];
		range_update(f->srng, s);

		range_calc_and_queue_event(f, s);

		range_update_consumer_events(f->drng, s);
	}


	//Log bandwidth usage
	struct node *n = c->peer[se->type];
	write_record(0, R_USAGE|se->type, n->node_id, -1,
		     &n->bandwidth_usage[se->type], s);

	//Log speed change
	write_record(0, R_SPD|se->type, se->c->conn_id, -1, &se->speed, s);
}

void speed_change_free(struct event *e, struct sim_state *s){
	struct spd_event *se = e->data;
	free(se);
}
