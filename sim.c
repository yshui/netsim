#define LOG_DOMAIN "sim_api"

#include "log.h"
#include "data.h"
#include "event.h"
#include "sim.h"
#include "flow.h"
#include "node.h"

void sim_send_packet(struct sim_state *s, void *data, int len,
		     struct node *src, struct node *dst){
	double delay = s->dlycalc(src->loction, dst->loction);
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
	int bandwidth = connect_create(src, dst);

	nf->begin_time = s->now;
	nf->start = start;
	nf->src = src;
	nf->dst = dst;
	nf->drng = node_new_range(dst, rid, start);
	nf->drng->producer = nf;
	nf->srng = rng;
	nf->resource_id = rid;
	nf->bandwidth = bandwidth;
	range_calc_flow_events(nf);
	list_add(rng->consumers, &nf->consumers);

	return 0;
}
