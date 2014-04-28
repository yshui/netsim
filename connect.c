#include <assert.h>

#include "data.h"
#include "event.h"
#include "range.h"

struct spd_event {
	struct event *e;
	struct connection *c;
	double speed;
	int close;
	enum peer_type type;
	struct list_head spd_evs;
};

static inline void queue_speed_event(struct connection *c, int dir, int close,
				     double speed, struct sim_state *s){
	struct spd_event *se = talloc(1, struct spd_event);
	se->type = dir;
	se->speed = speed;
	se->close = close;
	se->e = event_new(s->now+c->delay, SPEED_CHANGE, se);

	list_add(&se->spd_evs, &c->spd_evs);

	event_add(s, se->e);

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

	if (close)
		amount = -c->speed[dir];

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
			queue_speed_event(c, !dir, 0, -amount, s);
			return 0;
		}
		queue_speed_event(c, !dir, 0, share-c->speed[dir]-amount, s);
		amount = share-c->speed[dir];
	}

	c->speed[dir] += amount;
	/* Special Cases */
	if (total < max)
		//the connection can get its maximum/requested speed,
		//we know the speed limit of the peer won't be exceeded.
		return amount;

	if (amount > eps && used+amount < max) {
		//There're enough free bandwidth
		n->bandwidth_usage[dir] += amount;
		return amount;
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
		//lshare = the potential upbound of the connection
		//       = min(share of connection on this end,
		//	       share of connection on the other end)
		//	   when amount < 0 (so the speed is going to increase);
		//
		//	 = share of connection on this end
		//	   when amount > 0;
		double lshare = nc->bwupbound*total/max;
		if (amount < eps) {
			lshare = min(lshare, get_share(nc, !dir));
			if (nc->speed[dir] < lshare)
				e += lshare - nc->speed[dir];
		} else if (amount > eps && nc->speed[dir] > lshare)
			e += nc->speed[dir]-lshare;
	}

	if (amount < eps && -amount > e) {
		//Even we increase other connections' speed to their limit,
		//there are still free speed left.
		n->bandwidth_usage[dir] += amount+e;
		amount = -e;
	}
	list_for_each_entry(nc, h, conns[dir]){
		if (nc == c)
			continue;
		double lshare = nc->bwupbound*max/total;
		double delta;
		if (amount < eps) {
			lshare = min(lshare, get_share(nc, !dir));
			if (nc->speed[dir] < lshare) {
				delta = lshare - nc->speed[dir];
				nc->speed[dir] -= amount*delta/e;
				//queue speed increase event to the other end
				queue_speed_event(c, !dir, 0, -amount*delta/e, s);
			}
		} else if (amount > eps && nc->speed[dir] > lshare) {
			delta = nc->speed[dir]-lshare;
			nc->speed[dir] -= spread_amount*delta/e;
			//queue speed decrease event to the other end
			queue_speed_event(c, !dir, 0, -spread_amount*delta/e, s);
			//e > spread_amount is impossible, otherwise this
			//connection's speed would exceed its share.
		}
	}
	return spread_amount;
}

//outbound/src/snd = [0], inbound/dst/rcv = [1]
//dir = who initiate the close, 0 = the src, 1 = the dst
//I think the closing is always initiated by dst, but..
void connection_close(struct connection *c, int dir, struct sim_state *s){
	//Spread the bandwidth to remaining connections.
	bwspread(c, c->speed[dir], dir, 1, s);
	list_del(&c->conns[dir]);
	c->peer[dir]->total_bwupbound[0] -= c->bwupbound;
	//Queue event to notify the connection is closed.
	queue_speed_event(c, !dir, 1, c->speed[!dir], s);
}

//outbound/src/snd = [0], inbound/dst/rcv = [1]
//connection creation is always initiated by src
struct connection *connection_create(struct sim_state *s,
				     struct node *src, struct node *dst){
	struct connection *c = talloc(1, struct connection);
	c->bwupbound =
		s->bwcalc(src->user_data, dst->user_data);
	c->peer[0] = src;
	c->peer[1] = dst;
	c->speed[1] = 0;
	INIT_LIST_HEAD(&c->spd_evs);
	list_add(&c->conns[0], &src->conns[0]);
	src->total_bwupbound[0] += c->bwupbound;

	queue_speed_event(c, P_RCV, c->speed[0], P_RCV, s);

	return c;
}

#define abs(x) ((x)>0?(x):-(x))

void handle_speed_change(struct sim_state *s, struct event *e){
	struct spd_event *se = e->data;

	//Ignore events that are queued before our last event can reach the
	//other end.

	//By doing this, pending event will not be overwritten before it is
	//handled.
	if (e->qtime < se->c->pending_event[se->type])
		return;

	bwspread(se->c, se->speed-se->c->speed[se->type], se->type, se->close, s);
	//The pending event has been handled now.
	se->c->pending_event[!se->type] = 0;
	se->c->f->bandwidth = se->c->speed[se->type];

	if (se->type == P_RCV){
		//Update the flow and its drng
		struct range *rng = se->c->f->drng;
		rng->len += rng->grow*(s->now-rng->last_update);
		rng->last_update = s->now;
		se->c->f->bandwidth = rng->grow = se->c->speed[1];
		rng = se->c->f->srng;
		rng->len += rng->grow*(s->now-rng->last_update);
		rng->last_update = s->now;
		range_calc_flow_events(se->c->f);
		range_update_consumer_events(rng);
	}

	struct connection *c = se->c;
	if (se->close) {
		c->peer[se->type]->total_bwupbound[se->type] -= c->speed[se->type];
		struct list_head *h = &c->spd_evs;
		int dir = se->type;
		//Unqueue all of its events.
		while(!list_empty(h)){
			struct spd_event *se =
				list_first_entry(h, struct spd_event, spd_evs);
			list_del(h->next);
			list_del(&se->spd_evs);
			event_remove(se->e);
			free(se->e);
			free(se);
		}
		list_del(&c->conns[dir]);
		free(c);
	}
}
