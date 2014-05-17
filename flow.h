#pragma once

#include "data.h"

void flow_done_handler(struct event *e, struct sim_state *s);
void flow_done_cleaner(struct event *e, struct sim_state *s);
void flow_throttle_handler(struct event *e, struct sim_state *s);
