#pragma once

#include "data.h"
#include "log.h"
#include "record.h"
#include "range.h"

struct packet{
	struct node *src, *dst;
	void *data;
	int len;
};

struct node *sim_create_node(struct sim_state *s);
void sim_register_handler(int type, int priority, event_handler_func f,
			  struct sim_state *s);
struct flow *sim_establish_flow(id_t rid, size_t start, struct node *src,
				struct node *dst, struct sim_state *s);
void sim_send_packet(void *data, int len, struct node *src, struct node *dst,
		     struct sim_state *s);
struct resource *
sim_node_new_resource(struct node *n, size_t len);
struct resource *sim_node_add_resource(struct node *n, struct resource *r);

static inline void
sim_node_change_state(struct node *n, enum node_state state, struct sim_state *s){
	n->state = state;
	write_record(0, R_NODE_STATE, n->node_id, 1, &n->state, s);
}

static inline
void print_range(struct resource *r){
	struct skip_list_head *h = r->ranges.next[0];
	while(h){
		struct range *rng = skip_list_entry(h, struct range, ranges);
		assert(rng->start+rng->len <= rng->total_len);
		log_info("Range [%zu, %lf)\n", rng->start, rng->start+rng->len);
		h = h->next[0];
	}
}

static inline
void sim_end(struct sim_state *s){
	s->exit = true;
}

static inline
void node_del_resource(struct resource *r){
	range_clear(r);
	skip_list_deinit_head(&r->ranges);
	store_delete(r->owner->store, r);
	free(r);
}
