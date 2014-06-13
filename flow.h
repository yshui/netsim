#pragma once

#include "data.h"

struct spd_event {
	struct event *e;
	struct flow *f;
	double speed;
	enum peer_type type;
	struct list_head spd_evs;
};

void flow_close(struct flow *c, struct sim_state *s);

struct flow *flow_create(struct node *src, struct node *dst,
			 struct sim_state *s);

inline void
queue_speed_event(struct flow *c, int dir,
		  double speed, struct sim_state *s);

void handle_speed_change(struct event *e, struct sim_state *s);
void speed_change_free(struct event *e, struct sim_state *s);

void flow_done_handler(struct event *e, struct sim_state *s);
void flow_done_cleaner(struct event *e, struct sim_state *s);
void flow_throttle_handler(struct event *e, struct sim_state *s);

static inline bool
is_connected(struct node *src, struct node *dst){
	struct flow *tf = NULL;
	HASH_FIND(hh2, src->outs, &dst->node_id, sizeof(dst->node_id), tf);
	assert(!tf || tf->peer[1] == dst);
	return tf != NULL;
}

static inline double get_share(struct flow *f, int dir){
	struct node *n = f->peer[dir];
	double total = n->total_bwupbound[dir], max = n->maximum_bandwidth[dir];
	return total > max ? f->bwupbound*max/total : f->bwupbound;
}
