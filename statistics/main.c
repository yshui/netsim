#include <stdio.h>
#include <string.h>

#include "analyzer.h"
int main(int argc, const char **argv){
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <filename> <stats type>\n", argv[0]);
		return 1;
	}
	struct analyzer *p = analyzer_table;
	while(p->name) {
		int len = strlen(p->name);
		if (strncmp(argv[2], p->name, len) == 0)
			break;
	}

	if (!p->name) {
		fprintf(stderr, "No such statistic type %s\n", argv[2]);
		return 1;
	}
	struct record_handle *rh = open_record(argv[1]);
	void *sh = NULL;
	if (p->init)
		sh = p->init();
	struct record *r;
	while((r = read_record(rh)))
		p->next_record(sh, r);
	if (p->finish)
		p->finish(sh);
	return 0;
}
