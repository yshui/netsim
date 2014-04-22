#include "data.h"
#include "event.h"

struct spd_event {
	struct event *e;
	struct connection *c;
	int amount;
	enum {SC_SND, SC_RCV} type;
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

struct connection *connection_create(struct sim_state *s,
				     struct node *src, struct node *dst){
	struct connection *c = talloc(1, struct connection);
	int bw = c->snd_spd = c->upbound_bandwidth =
		s->bwcalc(src->loction, dst->loction);
	c->src = src;
	c->dst = dst;
	c->rcv_spd = 0;
	list_add(src->outbound_conn, &c->outs);
	if (bw+src->outbound_usage > src->outbound) {
		int now = bw+src->outbound_usage;
		struct connection *h;
		list_for_each_entry(h, src->outbound_conn, outs)
			h->snd_spd = h->snd_spd*src->outbound/now;
		src->outbound_usage = src->outbound;
	}else
		src->outbound_usage += bw;

	queue_spd_change(s, c, c->snd_spd, SC_RCV);

	return c;
}

#define min(a,b) ((a)<(b)?(a):(b))

#define bandwidth_spread_func(type,member) \
static int bandwidth_spread_##type##_##member(struct list_head *h, int freebw){ \
	int e = 0; \
	int freebw2 = freebw; \
	struct connection *c; \
	list_for_each_entry(c, h, member) \
		if (c->type < c->upbound_bandwidth) \
			e+=c->upbound_bandwidth-c->type; \
	list_for_each_entry(c, h, member) \
		if (c->type < c->upbound_bandwidth) { \
			int tmp = c->upbound_bandwidth-c->type; \
			int d = min(tmp, freebw*tmp/e); \
			c->type += d; \
			freebw2 -= d; \
		} \
	return freebw2; \
}

bandwidth_spread_func(snd_spd, outs)
bandwidth_spread_func(rcv_spd, ins)

void connection_close(struct connection *c){
	//Spread the bandwidth to remaining connections.
	list_del(&c->outs);
	int remainder = bandwidth_spread_snd_spd_outs(c->src->outbound_conn,
						      c->snd_spd);
	c->src->outbound_usage -= remainder;

	list_del(&c->ins);
	remainder = bandwidth_spread_rcv_spd_ins(c->dst->inbound_conn,
						 c->rcv_spd);
	c->dst->inbound_usage -= remainder;

	struct spd_event *se;
	list_for_each_entry(se, c->spd_evs, spd_evs){
		struct event *e = se->e;
		skip_list_delete_next(e->events.prev);
	}
}
