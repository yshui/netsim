#define LOG_DOMAIN "sim_api"

#include "log.h"
#include "data.h"
#include "event.h"
#include "sim.h"
#include "connect.h"
#include "node.h"

const double eps = 1e-6;
int global_log_level = LOG_INFO;

void sim_send_packet(struct sim_state *s, void *data, int len,
		     struct node *src, struct node *dst){
	double delay = s->dlycalc(src->user_data, dst->user_data);
	struct packet *p = calloc(1, sizeof(struct packet));
	p->src = src;
	p->dst = dst;
	p->data = data;
	p->len = len;
	struct event *e = event_new(delay, PACKET_DONE, p);
	event_add(s, e);
}

int sim_establish_flow(struct sim_state *s, int rid, int start,
			struct node *src, struct node *dst){
	struct resource *rsrc = store_get(dst->store, rid);
	struct range *rng = range_get(rsrc, start);
	if (rng) {
		log_err("Trying to create a flow to a existing range.");
		return -1;
	}

	struct flow *nf = calloc(1, sizeof(struct flow));
	struct resource *r = store_get(src->store, rid);
	if (!r)
		return -1;
	rng = range_get(r, start);
	struct connection *c = connection_create(s, src, dst);

	nf->begin_time = s->now;
	nf->start = start;
	nf->src = src;
	nf->dst = dst;
	nf->drng = node_new_range(dst, rid, start);
	nf->drng->producer = nf;
	nf->srng = rng;
	nf->resource_id = rid;
	nf->bandwidth = c->speed[1];
	range_calc_flow_events(nf);
	list_add(&nf->consumers, &rng->consumers);

	return 0;
}

void sim_register_handler(int type, int priority, event_handler_func f,
			  struct sim_state *s){
	struct list_head *h = &s->handlers[type];
	struct event_handler *eh;
	list_for_each_entry(eh, h, handlers)
		if (eh->pri > priority)
			break;

	struct event_handler *neh = talloc(1, struct event_handler);
	neh->f = f;
	neh->pri = priority;
	if (eh == NULL)
		list_add(&neh->handlers, h->prev);
	else
		list_add(&neh->handlers, eh->handlers.prev);
}
