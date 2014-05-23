#define LOG_DOMAIN "sim_api"

#include "log.h"
#include "data.h"
#include "event.h"
#include "sim.h"
#include "flow.h"
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

struct flow *sim_establish_flow(id_t rid, size_t start, struct node *src, struct node *dst,
		       struct sim_state *s){
	struct resource *sr = store_get(src->store, rid);
	if (!sr) {
		log_err("The resource %d doesn't exist on source node %d\n", rid, src->node_id);
		return NULL;
	}
	struct resource *dr = store_get(dst->store, rid);
	if (!dr) {
		//The resource doesn't exist on the dst yet, create it
		dr = resource_new(sr->resource_id, sr->len);
		dr->bit_rate = sr->bit_rate;
		dr->owner = dst;
		store_set(dst->store, dr);
	}
	struct range *rng = range_get(dr, start);
	if (rng) {
		log_err("Trying to create a flow to a existing range.\n");
		return NULL;
	}

	rng = range_get(sr, start);
	struct flow *nf = flow_create(src, dst, s);

	nf->begin_time = s->now;
	nf->start = start;
	nf->drng = node_new_range(dst, rid, start, 0);
	nf->drng->producer = nf;
	nf->drng->last_update = s->now;
	nf->srng = rng;
	nf->resource_id = rid;
	range_calc_and_requeue_events(nf, s);
	list_add(&nf->consumers, &rng->consumers);

	//update prev range's events
	struct skip_list_head *ph = nf->drng->ranges.prev[0];
	if (ph != &dr->ranges) {
		//Not the first range
		struct range *prng = skip_list_entry(ph, struct range, ranges);
		range_calc_and_requeue_events(prng->producer, s);
	}

	return nf;
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
struct resource *sim_node_new_resource(struct node *n, size_t len){
	if (!n->store)
		n->store = store_new();
	struct resource *r = resource_new(random(), len);
	r->owner = n;

	while(store_set(n->store, r) == -1)
		r->resource_id = random();

	resource_new_range(r, 0, len);
	return r;
}
struct resource *sim_node_add_resource(struct node *n, struct resource *r){
	if (!n->store)
		n->store = store_new();
	struct resource *nr = store_get(n->store, r->resource_id);
	if (nr)
		return NULL;
	nr = resource_new(r->resource_id, r->len);
	nr->bit_rate = r->bit_rate;
	nr->owner = n;

	store_set(n->store, nr);
	resource_new_range(nr, 0, nr->len);
	return nr;
}
