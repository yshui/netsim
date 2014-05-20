#pragma once

#include "data.h"

void user_done(struct event *e, struct sim_state *s);
void user_speed_change(struct event *e, struct sim_state *s);
void user_lowwm_event(struct range *rng, struct sim_state *s);
void user_highwm_event(struct range *rng, struct sim_state *s);
void user_recalculate_event2(struct def_user *d, struct sim_state *s);
