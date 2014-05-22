#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>

#define LOG_DOMAIN "main"

#include "log.h"
#include "data.h"
#include "skiplist.h"
#include "event.h"
#include "list.h"
#include "sim.h"
#include "flow.h"

int global_log_level = LOG_INFO;

int main(int argc, const char **argv){
	if (argc != 2){
		fprintf(stderr, "Usage: %s module\n", argv[0]);
		return 1;
	}

	char *filename = strdup(argv[1]);
	char *tmp = malloc(strlen(filename)+6);
	sprintf(tmp, "%s.so", filename);
	void *mhandle = dlopen(tmp, RTLD_LAZY);
	if (!mhandle) {
		fprintf(stderr, "%s\n", dlerror());
		return 1;
	}

	sprintf(tmp, "%s_init", basename(filename));
	void (*minit)(struct sim_state *s) = dlsym(mhandle, tmp);

	struct sim_state *s = sim_state_new();
	minit(s);

	free(filename);
	free(tmp);

	//Register some default handlers
	sim_register_handler(SPEED_CHANGE, HNDR_DEFAULT, handle_speed_change, s);
	sim_register_handler(SPEED_CHANGE, HNDR_CLEANER, speed_change_free, s);
	sim_register_handler(FLOW_DONE, HNDR_DEFAULT, flow_done_handler, s);
	sim_register_handler(FLOW_DONE, HNDR_CLEANER, flow_done_cleaner, s);
	sim_register_handler(FLOW_SPEED_THROTTLE, HNDR_DEFAULT, flow_throttle_handler, s);

	while(!skip_list_empty(&s->events)){
		struct event *e = event_pop(s);
		if (s->now > e->time) {
			log_err("Time travaling! %lf -> %lf\n", s->now, e->time);
			abort();
		}
		s->now = e->time;
		log_debug("Pop event %d %lf\n", e->type, e->time);
		struct event_handler *eh;
		list_for_each_entry(eh, &s->handlers[e->type], handlers)
			eh->f(e, s);
	}
	return 0;
}
