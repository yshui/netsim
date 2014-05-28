#pragma once
#include "data.h"

void new_resource_handler1(id_t rid, bool delay, struct sim_state *s);
void cloud_online(struct node *n, struct sim_state *s);
void cloud_offline(struct node *n, struct sim_state *s);
void new_connection_handler1(struct node *cld, id_t rid, struct sim_state *s);
void new_connection_handler2(struct node *cld, id_t rid, struct sim_state *s);
