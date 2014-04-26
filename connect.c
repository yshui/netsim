#include <assert.h>

#include "data.h"
#include "event.h"

struct spd_event {
	struct event *e;
	struct connection *c;
	int amount, close;
	enum {SC_SND = 0, SC_RCV} type;
	struct list_head spd_evs;
};

static inline void queue_spd_change(struct sim_state *s, struct connection *c,
				    int amount, int type){
	struct spd_event *se = talloc(1, struct spd_event);
	se->c = c;
	se->amount = amount;
	se->type = type;
	se->e = event_new(s->dlycalc(c->src->loction, c->dst->loction),
				    SPEED_CHANGE, se);
	event_add(s, se->e);

	list_add(c->spd_evs, &se->spd_evs);
}

//outbound/src/snd = [0], inbound/dst/rcv = [1]
struct connection *connection_create(struct sim_state *s,
				     struct node *src, struct node *dst){
	struct connection *c = talloc(1, struct connection);
	int bw = c->speed[0] = c->bwupbound =
		s->bwcalc(src->loction, dst->loction);
	c->peer[0] = src;
	c->peer[1] = dst;
	c->speed[1] = 0;
	list_add(src->conns[0], &c->conns[0]);
	src->total_bwupbound[0] += bw;
	if (src->total_bwupbound[0] > src->maximum_bandwidth[0]) {
		double share = bw*src->maximum_bandwidth[0]/src->total_bwupbound[0];
		//update all the shares;
	}else
		c->speed[0] = bw;

	queue_spd_change(s, c, c->speed[0], SC_RCV);

	return c;
}


#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

static inline double get_share(struct connection *c, int dir){
	struct node *n = c->peer[dir];
	double total = n->total_bwupbound[dir], max = n->maximum_bandwidth[dir];
	double used = n->bandwidth_usage[dir];
	return total > max ? c->bwupbound*max/total : c->bwupbound;
}

static inline void queue_speed_event(struct connection *c, int dir, int close,
				     double amount, struct sim_state *s){
	struct spd_event *se = talloc(1, struct spd_event);
	se->type = dir;
	se->amount = amount;
	se->close = close;
	struct event *e = event_new(s->now, SPEED_CHANGE, se);
	se->e = e;

	list_add(c->spd_evs, &se->spd_evs);

	event_add(s, e);
}

//Return the actual amount changed.
static inline double bwspread(struct connection *c, double amount, int dir,
			      int close, struct sim_state *s){
	//Negative amount is always fulfilled (assuming the amount is sane, i.e.
	//-amount < c->speed[dir]), while positive amount is not.

	//Speed change is almost always due to the other end's speed is changed,
	//with the exception of connection closing.

	//So I don't queue event to notify the other end of speed change, since
	//they should already know, I queue event only when the speed change
	//can't be fulfilled.

	if (close)
		amount = -c->speed[dir];
	struct node *n = c->peer[dir];
	struct list_head *h = n->conns[dir];
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
			}
		} else if (amount > eps && nc->speed[dir] > lshare) {
			delta = nc->speed[dir]-lshare;
			nc->speed[dir] -= spread_amount*delta/e;
			//e > spread_amount is impossible, otherwise this
			//connection's speed would exceed its share.
			//queue speed decrease event to the other end
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
	//Queue event to notify the connection is closed.
	queue_speed_event(c, !dir, 1, c->speed[!dir], s);
}

#define abs(x) ((x)>0?(x):-(x))

void handle_speed_change(struct sim_state *s, struct event *e){
	struct spd_event *se = e->data;
	if (se->close) {

	double remainder;
	switch(se->type){
		case SC_RCV:
			assert(se->amount > 0);
			assert(se->amount < se->c->dst->inbound);
			remainder = bwspread_rcv_spd_ins(se->c->dst->inbound_conn,
							 se->c, -se->amount);
			assert(abs(remainder) <= eps);
			se->c->rcv_spd += se->amount;
			range_update_rcv_spd(se->c);
			break;
		case SC_SND:
			assert(se->amount < 0);
			remainder = bwspread_snd_spd_outs(se->c->src->outbound_conn,
							  se->c, -se->amount);
			se->c->src->outbound_usage -= remainder;
			se->c->snd_spd += se->amount;
			range_update_snd_spd(se->c);
			break;
	}
}
