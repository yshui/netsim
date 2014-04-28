#pragma once

#include "data.h"

double bwspread(struct connection *c, double amount, int dir,
		int close, struct sim_state *s);

void connection_close(struct connection *c, int dir, struct sim_state *s);

struct connection *connection_create(struct sim_state *s,
				     struct node *src, struct node *dst);
