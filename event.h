#pragma once
#include "skiplist.h"
#include "data.h"

static inline void event_remove(struct event *e){
	if (!e)
		return;
	e->qtime = -1;
	skip_list_delete_next(e->events.prev);
}

static inline struct event *event_pop(struct sim_state *s){
	struct event *e = skip_list_entry(s->events.next[0], struct event, events);
	skip_list_delete_next(&s->events);
	return e;
}

static inline int event_cmp(struct skip_list_head *h, void *_key){
	struct event *e = skip_list_entry(h, struct event, events);
	double key = *(double *)_key;
	return e->time > key ? 1 : -1;
}

static inline void event_add(struct event *e, struct sim_state *s){
	e->qtime = s->now;
	skip_list_insert(&s->events, &e->events, &e->time, event_cmp);
}
