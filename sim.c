#define LOG_DOMAIN "sim_api"

#include "log.h"
#include "data.h"
#include "event.h"
#include "sim.h"
#include "connect.h"
#include "range.h"

const double eps = 1e-6;

void sim_send_packet(void *data, int len, struct node *src, struct node *dst,
		     struct sim_state *s){
	double delay = s->dlycalc(src->user_data, dst->user_data);
	struct packet *p = calloc(1, sizeof(struct packet));
	p->src = src;
	p->dst = dst;
	p->data = data;
	p->len = len;
	struct event *e = event_new(delay, PACKET_DONE, p);
	event_add(e, s);
}

int sim_establish_flow(id_t rid, size_t start, struct node *src, struct node *dst,
		       struct sim_state *s){
	struct resource *sr = store_get(src->store, rid);
	if (!sr) {
		log_err("The resource %d doesn't exist on source node %d\n", rid, src->node_id);
		return -1;
	}
	struct resource *dr = store_get(dst->store, rid);
	if (!dr) {
		//The resource doesn't exist on the dst yet, create it
		dr = resource_new(sr->resource_id, sr->len);
		store_set(dst->store, dr);
	}
	struct range *rng = range_get(dr, start);
	if (rng) {
		log_err("Trying to create a flow to a existing range.\n");
		return -1;
	}

	struct flow *nf = talloc(1, struct flow);
	rng = range_get(sr, start);
	struct connection *c = connection_create(src, dst, s);

	nf->begin_time = s->now;
	nf->start = start;
	nf->src = src;
	nf->dst = dst;
	nf->drng = node_new_range(dst, rid, start, 0);
	nf->drng->producer = nf;
	nf->srng = rng;
	nf->resource_id = rid;
	nf->bandwidth = 0;
	nf->c = c;
	c->f = nf;
	range_calc_flow_events(nf, s->now);
	list_add(&nf->consumers, &rng->consumers);

	id_t rand = random();
	struct flow *of;
	do {
		HASH_FIND_INT(s->flows, &rand, of);
	}while(of);
	nf->flow_id = rand;
	HASH_ADD_INT(s->flows, flow_id, nf);

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

struct node *sim_create_node(struct sim_state *s){
	struct node *n = node_new(), *on;

	do {
		n->node_id = random();
		HASH_FIND_INT(s->nodes, &n->node_id, on);
	}while(on);
	HASH_ADD_INT(s->nodes, node_id, n);

	return n;
}

//Create a new resource on a node, this node will immediately have a whole
//copy of the resource
struct resource *sim_node_new_resource(struct node *n, size_t len, struct sim_state *s){
	if (!n->store)
		n->store = store_new();
	struct resource *r = resource_new(random(), len);

	while(store_set(n->store, r) == -1)
		r->resource_id = random();

	resource_new_range(r, 0, len);
	return r;
}
