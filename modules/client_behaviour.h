#pragma once
#include "data.h"

void client_next_state_from_event(struct event *e, struct sim_state *s);
void client_handle_next_state(struct node *n, struct sim_state *s);
int client_new_connection(id_t rid, size_t start, struct node *server,
			  struct node *client, struct sim_state *s);
void client_start_play(struct node *client, id_t rid, struct sim_state *s);

