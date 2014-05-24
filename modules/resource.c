#include "resource.h"
#include "list.h"
#include "gaussian.h"
#include "p2p_common.h"
#include "event.h"
#include "store.h"

#include <stdlib.h>

void next_resource_event(struct sim_state *s){
	struct def_sim *ds = s->user_data;
	double r1 = random()/((double)RAND_MAX);
	struct skip_list_head *h = skip_list_find_ge(&ds->rms, &r1, resource_model_cmp);
	struct resource_model *r = skip_list_entry(h, struct resource_model, models);
	double time = gaussian_noise(ds->tvar, ds->tm);
	struct user_event *ue = talloc(1, struct user_event);
	ue->type = NEW_RESOURCE;
	ue->data = r;
	struct event *e = event_new(s->now+time, USER, ue);
	event_add(e, s);
}

id_t new_resource(struct resource_model *r, struct sim_state *s){
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
	//rlen is the length in seconds
	//the size is length*bit_rate
	struct resource *nr = resource_new(re->resource_id, rlen*rbr);
	nr->bit_rate = rbr;

	struct server *svr;
	list_for_each_entry(svr, &ds->servers, servers)
		sim_node_add_resource(svr->n, nr);

	//Add to the head of probs
	if (ds->nrsrc == ds->max_rsrc) {
		//Number exceeded, remove the lowest ranked rsrc
		struct list_head *h = ds->rsrc_probs.prev;
		struct resource_entry *re = list_entry(h, struct resource_entry, probs);
		HASH_DEL(ds->rsrcs, re);
		list_del(h);
	}else
		ds->nrsrc++;
	int new_rank = random()%ds->nrsrc;
	struct list_head *h = &ds->rsrc_probs;
	while(new_rank--)
		h = h->next;
	list_add(&re->probs, h);

	int rank;
	struct resource_entry *tre;
	double C = 0;
	for (rank = 1; rank <= ds->nrsrc; rank++)
		C += 1/((double)rank);

	C = 1/C;
	rank = 1;
	list_for_each_entry(tre, &ds->rsrc_probs, probs) {
		tre->prob = C/((double)rank); //zipf distribution
		rank++;
	}

	id_t ret = nr->resource_id;
	free(nr);
	return ret;
}

id_t new_resource_random(struct sim_state *s){
	double rand = random()/((double)RAND_MAX);
	struct def_sim *ds = s->user_data;
	struct skip_list_head *h = skip_list_find_ge(&ds->rms, &rand, resource_model_cmp);
	struct resource_model *rm = skip_list_entry(h, struct resource_model, models);
	return new_resource(rm, s);
}

void resource_add_provider(id_t rid, struct node *n, struct sim_state *s){
	struct def_sim *ds = s->user_data;
	if (!ds)
		return;

	struct resource_entry *re = NULL;
	HASH_FIND_INT(ds->rsrcs, &rid, re);
	if (!re)
		return;

	struct resource_provider *tp = NULL;
	HASH_FIND_INT(re->holders, &n->node_id, tp);
	if (tp)
		return;

	struct resource *r = store_get(n->store, rid);
	if (!r)
		return;

	struct resource_provider *p = talloc(1, struct resource_provider);
	p->r = r;
	p->node_id = n->node_id;
	HASH_ADD_INT(re->holders, node_id, p);
}

void resource_del_provider(int resource_id, int node_id, struct sim_state *s){
	struct def_sim *ds = s->user_data;
	if (!ds)
		return;

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

id_t resource_picker(struct sim_state *s){
	double rand = random()/((double)RAND_MAX);
	struct resource_entry *re;
	struct def_sim *ds = s->user_data;
	list_for_each_entry(re, &ds->rsrc_probs, probs){
		if (rand < re->prob)
			break;
		rand -= re->prob;
	}
	return re->resource_id;
}

