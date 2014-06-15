#pragma once
#include "data.h"
#include "range.h"
#include "flow.h"
#include "p2p_common.h"
#include "qselect.h"
#include "gaussian.h"

typedef int (*eval_func)(struct node *n, void *data);

void client_next_state_from_event(struct event *e, struct sim_state *s);
void client_handle_next_state(struct node *n, struct sim_state *s);
int client_new_connection(id_t rid, size_t start, struct node *server,
			  struct node *client, struct sim_state *s);
void client_start_play(struct node *client, id_t rid, struct sim_state *s);
void client_done(struct event *e, struct sim_state *s);
void client_speed_change(struct event *e, struct sim_state *s);
void client_lowwm_event(struct range *rng, struct sim_state *s);
void client_highwm_event(struct range *rng, struct sim_state *s);
void client_speed_throttle(struct event *e, struct sim_state *s);
void client_next_event(struct node *n, struct sim_state *s);
void client_new_play1(id_t, struct node *n, struct sim_state *s);
void client_new_play2(id_t rid, struct node *n, bool use_client, struct sim_state *s);
struct node *
server_picker1(struct node *client, struct sim_state *s);
struct node *
server_picker2(id_t rid, size_t start, struct node *client, bool use_client, struct sim_state *s);

static inline bool
is_resource_usable(struct resource *r, size_t start, bool client, struct sim_state *s){
	struct range *rng = range_get_by_start(r, start);
	_range_update(rng, s);
	if (rng->start+rng->len <= start)
		return false;
	if (r->owner->state == N_OFFLINE || r->owner->state == N_DYING)
		return false;
	if (!client && r->owner->state != N_CLOUD)
		return false;
	return true;
}

static inline bool
is_node_usable(struct node *n, id_t rid, size_t start, bool client, struct sim_state *s){
	struct resource *r = store_get(n->store, rid);
	assert(r->owner == n);
	if (!r)
		return false;
	return is_resource_usable(r, start, client, s);
}

#define cmp(a, b) ((a)->val-(b)->val)
def_qselect(eval, struct nv_pair);
#undef cmp


static inline void
server_picker_opt1(struct node *client, eval_func opt, int *count,
		  void *data, struct sim_state *s){
	//Choose only server nodes
	struct def_sim *ds = s->user_data;
	struct server *ss = NULL;

	int c = 0;
	struct node *res = NULL;
	list_for_each_entry(ss, &ds->servers, servers) {
		if (!is_connected(ss->n, client)) {
			ds->eval_table[c].val = opt(ss->n, data);
			ds->eval_table[c++].n = ss->n;
		}
	}
	if (c <= *count) {
		*count = c;
		return;
	}
	qselect_eval(ds->eval_table, c, *count);
}

static inline struct node *
server_picker_opt2(id_t rid, size_t start, struct node *client, eval_func opt,
		   void *data, bool use_client, struct sim_state *s){
	//Choose any non-server nodes
	struct def_sim *ds = s->user_data;
	struct resource_entry *re = NULL;
	HASH_FIND_INT(ds->rsrcs, &rid, re);
	if (!re)
		return NULL;

	int min = INT32_MAX;
	int cnt1 = 0, cnt2 = 0;
	struct node *res = NULL;
	struct resource_provider *rp, *tmp;
	HASH_ITER(hh, re->holders, rp, tmp) {
		if (!is_resource_usable(rp->r, start, use_client, s)) {
			cnt1++;
			continue;
		}
		if(is_connected(rp->r->owner, client)) {
			cnt2++;
			continue;
		}
		int val = opt(rp->r->owner, data);
		if (val < min) {
			res = rp->r->owner;
			min = val;
		}
	}
	if (!res && start == 0) {
		log_warning("[%lf] Warning: no candidate found for rid %d created at %lf\n", s->now, rid, re->ctime);
		log_warning("resource not usable: %d connected: %d\n", cnt1, cnt2);
	}
	return res;
}

static inline double
get_break_by_hour(int hour){
	if (hour > 12 && hour < 20)
		return gaussian_noise_nz(150, 1091);
	else
		return gaussian_noise_nz(900, 4173);
}

static inline bool
is_busy_hour(int hour){
	if (hour > 12 && hour < 20)
		return true;
	else
		return false;
}
