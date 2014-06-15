#pragma once

#ifndef LOG_DOMAIN
# define LOG_DOMAIN "event"
#endif

#include "log.h"
#include "skiplist.h"
#include "data.h"

#include <math.h>

static inline void event_remove(struct event *e){
	if (!e || !e->active)
		return;
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
#ifndef NDEBUG
static inline bool _event_fsck(struct sim_state *s){
	struct skip_list_head *h = s->events.next[0];
	if (!h)
		return true;
	double last = 0;
	while(h) {
		struct event *e = skip_list_entry(h, struct event, events);
		assert(e->time >= last);
		assert(e->_fsck == false);
		last = e->time;
		h = h->next[0];
		e->_fsck = true;
	}
	h = s->events.next[0];
	while(h) {
		struct event *e = skip_list_entry(h, struct event, events);
		h = h->next[0];
		e->_fsck = false;
	}
	return true;
}
#endif

static inline void event_add(struct event *e, struct sim_state *s){
	//assert(_event_fsck(s));
	if (!e || e->active)
		return;
	if (e->time < s->now) {
		if (e->time < s->now-eps) {
			log_err("Add event back in time, e: %lf now: %lf\n", e->time, s->now);
			abort();
		}
		e->time = s->now;
	}
	if (isnan(e->time)) {
		log_err("Add event at nan\n");
		abort();
	}
	log_debug("[%lf] Event add %d %lf\n", s->now, e->type, e->time);
	e->active = true;
	skip_list_insert(&s->events, &e->events, &e->time, event_cmp);
}

static inline bool is_active(struct event *e){
	if (!e)
		return false;
	return e->active;
}

static inline bool is_later_than(double time, struct event *b){
	if (!b->active)
		return false;
	if (time > b->time+eps)
		return true;
	return false;
}
