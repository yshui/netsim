#include <assert.h>

#include "connect.h"
#include "event.h"
#include "range.h"
#include "record.h"

extern inline void
queue_speed_event(struct connection *c, int dir, int close,
		  double speed, struct sim_state *s){
	struct spd_event *se = talloc(1, struct spd_event);
	se->type = dir;
	se->speed = speed;
	se->close = close;
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
			queue_speed_event(c, !dir, 0, c->speed[dir], s);
			return 0;
		}
		queue_speed_event(c, !dir, 0, share, s);
		amount = share-c->speed[dir];
	}

	c->speed[dir] += amount;
	/* Special Cases */
	if (total < max)
		//the connection can get its maximum/requested speed,
		//we know the speed limit of the peer won't be exceeded.
		return amount;

	if (amount > eps && used+amount < max+eps) {
		//There're enough free bandwidth
		n->bandwidth_usage[dir] += amount;
		return amount;
	}
	if (used < max)
		n->bandwidth_usage[dir] = n->maximum_bandwidth[dir];

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
		//       = share of connection on the other end)
		//	   when amount < 0 (so the speed is going to increase);
		//
		//	 = share of connection on this end
		//	   when amount > 0;
		double lshare = nc->bwupbound*max/total;
		if (amount < eps) {
			lshare = get_share(nc, !dir);
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
				queue_speed_event(nc, !dir, 0, nc->speed[dir], s);
			}
		} else if (amount > eps && nc->speed[dir] > lshare) {
			delta = nc->speed[dir]-lshare;
			nc->speed[dir] -= spread_amount*delta/e;
			//queue speed decrease event to the other end
			queue_speed_event(nc, !dir, 0, nc->speed[dir], s);
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

	//Remove the corresponding flow from consumer
	struct flow *f = c->f;
	list_del(&f->consumers);

	//Log connection close
	uint8_t type = dir;
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
	INIT_LIST_HEAD(&c->spd_evs);
	list_add(&c->conns[0], &src->conns[0]);
	list_add(&c->conns[1], &dst->conns[1]);
	src->total_bwupbound[0] += c->bwupbound;
	dst->total_bwupbound[1] += c->bwupbound;

	double spd0 = get_share(c, 0);
	bwspread(c, spd0, 0, 0, s);

	queue_speed_event(c, P_RCV, 0, c->speed[0], s);

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

	bwspread(se->c, se->speed-se->c->speed[se->type], se->type, se->close, s);
	//The pending event has been handled now.
	c->pending_event[!se->type] = 0;
	f->bandwidth = c->speed[se->type];

	list_del(&se->spd_evs);

	if (se->type == P_RCV){
		//Update the flow and its drng
		struct range *rng = f->drng;
		rng->len += rng->grow*(s->now-rng->last_update);
		rng->last_update = s->now;
		f->bandwidth = rng->grow = c->speed[1];
		rng = f->srng;
		rng->len += rng->grow*(s->now-rng->last_update);
		rng->last_update = s->now;

		event_remove(f->done);
		event_remove(f->drain);
		free(f->done);
		free(f->drain);
		range_calc_flow_events(f, s->now);
		event_add(f->done, s);
		event_add(f->drain, s);

		range_update_consumer_events(f->drng, s);
	}


	//Log bandwidth usage
	struct node *n = c->peer[se->type];
	write_record(0, R_USAGE|se->type, n->node_id, -1,
		     &n->bandwidth_usage[se->type], s);

	if (se->close) {
		c->peer[se->type]->total_bwupbound[se->type] -= c->speed[se->type];
		struct list_head *h = &c->spd_evs;
		int dir = se->type;
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

		//Log connection close
		uint8_t type = se->type;
		write_record(0, R_CONN_CLOSE|se->type, se->c->conn_id, 1, &type, s);

		//Close the flow
		f->drng->grow = 0;
		f->drng->producer = NULL;
		event_remove(f->done);
		event_remove(f->drain);
		free(f->done);
		free(f->drain);
		HASH_DEL(s->flows, f);
		free(f);

		//Close the corresponding flow
		list_del(&c->conns[dir]);
		HASH_DEL(s->conns, c);
		free(c);
	}else
		//Log speed change
		write_record(0, R_SPD|se->type, se->c->conn_id, -1, &se->speed, s);
}
