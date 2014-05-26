#pragma once
#include "record.h"
//Some commonly used record writers
//It's find to write redundant records, we are relying on the analyzer
//to deduplicate.
static void
write_usage(int dir, struct node *n, struct sim_state *s){
	write_record(0, R_USAGE|dir, n->node_id, -1, &n->bandwidth_usage[dir], s);
}
