#pragma once
#include "data.h"
#include "p2p_common.h"

void client_next_state_from_event(struct event *e, struct sim_state *s);
void client_handle_next_state(struct node *n, struct sim_state *s);
int client_new_connection(id_t rid, size_t start, struct node *server,
			  struct node *client, struct sim_state *s);
void client_start_play(struct node *client, id_t rid, struct sim_state *s);
void client_done(struct event *e, struct sim_state *s);
void client_speed_change(struct event *e, struct sim_state *s);
void client_lowwm_event(struct range *rng, struct sim_state *s);
void client_highwm_event(struct range *rng, struct sim_state *s);

