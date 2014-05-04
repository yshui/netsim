#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>

#include "data.h"
#include "skiplist.h"
#include "event.h"
#include "list.h"

int main(int argc, const char **argv){
	if (argc != 2){
		fprintf(stderr, "Usage: %s module\n", argv[0]);
		return 1;
	}

	char *tmp = malloc(strlen(argv[1])+6);
	sprintf(tmp, "%s.so", argv[1]);
	void *mhandle = dlopen(tmp, RTLD_LAZY);
	sprintf(tmp, "%s_init", argv[1]);
	void (*minit)(struct sim_state *s) = dlsym(mhandle, tmp);

	struct sim_state *s = sim_state_new();
	minit(s);

	while(!skip_list_empty(&s->events)){
		struct event *e = event_pop(s);
		struct event_handler *eh;
		list_for_each_entry(eh, &s->handlers[e->type], handlers)
			eh->f(e, s);
	}
	return 0;
}
