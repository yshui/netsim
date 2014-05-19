#pragma once

#ifndef LOG_DOMAIN
# define LOG_DOMAIN "event"
#endif

#include "log.h"
#include "skiplist.h"
#include "data.h"

static inline void event_remove(struct event *e){
	if (!e || !e->active)
		return;
	e->qtime = -1;
	e->active = false;
	skip_list_delete(&e->events);
}

static inline struct event *event_pop(struct sim_state *s){
	struct event *e = skip_list_entry(s->events.next[0], struct event, events);
	skip_list_delete(&e->events);
	e->active = false;
	return e;
}

static inline int event_cmp(struct skip_list_head *h, void *_key){
	struct event *e = skip_list_entry(h, struct event, events);
	double key = *(double *)_key;
	return e->time > key ? 1 : -1;
}

static inline void event_add(struct event *e, struct sim_state *s){
	if (!e || e->active)
		return;
	if (e->time < s->now) {
		log_err("Add event back in time\n");
		abort();
	}
	log_debug("Event add %d %lf\n", e->type, e->time);
	e->active = true;
	e->qtime = s->now;
	skip_list_insert(&s->events, &e->events, &e->time, event_cmp);
}
