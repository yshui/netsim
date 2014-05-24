#pragma once
#include "data.h"

void new_resource_handler1(id_t rid, struct sim_state *s);
void cloud_online(struct node *n, struct sim_state *s);
void cloud_offline(struct node *n, struct sim_state *s);
