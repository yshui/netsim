#include "resource.h"
#include "skiplist.h"
#include "gaussian.h"
#include "p2p_common.h"
#include "event.h"

#include <stdlib.h>

extern struct skip_list_head rm;

void next_resource_event(struct sim_state *s){
	double r1 = random()/((double)RAND_MAX);
	struct skip_list_head *h = skip_list_find(&rm, &r1, resource_model_cmp);
	struct resource_model *r = skip_list_entry(h, struct resource_model, models);
	double time = gaussian_noise(r->tvar, r->tm);
	struct user_event *ue = talloc(1, struct user_event);
	ue->type = NEW_RESOURCE;
	ue->data = r;
	struct event *e = event_new(s->now+time, USER, ue);
	event_add(e, s);
}

void new_resource(struct event *e, struct sim_state *s){
	struct user_event *ue = e->data;
	struct resource_model *r = ue->data;
	struct def_sim *ds = s->user_data;
	double rlen = gaussian_noise(r->lvar, r->lm);
	double rbr = gaussian_noise(r->brvar, r->brm);
	struct resource_entry *re = talloc(1, struct resource_entry);
	struct resource_entry *nre = NULL;
	do {
		re->resource_id = random();
		HASH_FIND_INT(ds->rsrcs, &re->resource_id, nre);
	}while(nre);
	HASH_ADD_INT(ds->rsrcs, resource_id, re);
	struct resource *nr = resource_new(re->resource_id, rlen);
	nr->bit_rate = rbr;

	struct server *svr;
	list_for_each_entry(svr, &ds->servers, servers)
		sim_node_add_resource(svr->n, nr);

	//Add to the head of probs
	list_add(&ds->rsrc_probs, &re->probs);
	//Remove the oldest resource

	int rank;
	struct resource_entry *tre;
	list_for_each_entry(tre, &ds->rsrc_probs, probs)
		tre->prob = 000000; //XXX
	free(nr);
}

void resource_add_provider(int resource_id, struct node *n, struct sim_state *s){
	struct def_sim *ds = s->user_data;
	struct resource_entry *re = NULL;
	HASH_FIND_INT(ds->rsrcs, &resource_id, re);
	if (!re)
		return;

	struct resource_provider *tp = NULL;
	HASH_FIND_INT(re->holders, &n->node_id, tp);
	if (tp)
		return;

	struct resource_provider *p = talloc(1, struct resource_provider);
	p->n = n;
	p->node_id = n->node_id;
	HASH_ADD_INT(re->holders, node_id, p);
}

void resource_del_provider(int resource_id, int node_id, struct sim_state *s){
	struct def_sim *ds = s->user_data;
	struct resource_entry *re = NULL;
	HASH_FIND_INT(ds->rsrcs, &resource_id, re);
	if (!re)
		return;

	struct resource_provider *p = NULL;
	HASH_FIND_INT(re->holders, &node_id, p);
	if (!p)
		return;

	HASH_DEL(re->holders, p);
	free(p);
}
