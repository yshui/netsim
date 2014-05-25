#include <stdio.h>

#include "record.h"
#include "analyzer.h"

void test_next_record(void *data, struct record *r){
	printf("[%.06lf] ", r->time);
	if (r->major == 0)
		printf("%s ", strrtype(r->minor));
	else
		printf("User event ");
	printf("%u data:", r->id);

	if (r->minor == R_NODE_STATE) {
		printf(" New state %s\n", strstate(r->a.u8));
		return;
	}
	switch(r->type){
		case U8:
			printf(" U8 %u\n", r->a.u8);
			break;
		case U16:
			printf(" U16 %u\n", r->a.u16);
			break;
		case U32:
			printf(" U32 %u\n", r->a.u32);
			break;
		case DOUBLE:
			printf(" DOUBLE %lf\n", r->a.d);
			break;
		default:
			printf(" INVALID\n");
	}
}

struct analyzer analyzer_table[] = {
	{"test", NULL, test_next_record, NULL},
	{NULL, NULL, NULL},
};
