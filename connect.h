#pragma once

#include "data.h"

struct spd_event {
	struct event *e;
	struct connection *c;
	double speed;
	int close;
	enum peer_type type;
	struct list_head spd_evs;
};

double bwspread(struct connection *c, double amount, int dir,
		int close, struct sim_state *s);

void connection_close(struct connection *c, int dir, struct sim_state *s);

struct connection *connection_create(struct node *src, struct node *dst,
				     struct sim_state *s);

struct event *
connection_speed_change_new(struct connection *c, double new_speed,
			    struct sim_state *s);

inline void
queue_speed_event(struct connection *c, int dir, int close,
		  double speed, struct sim_state *s);
