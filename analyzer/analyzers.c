#include <stdio.h>
#include <assert.h>

#include "record_reader.h"
#include "analyzers.h"
#include "uthash.h"
#include "list.h"
#include "skiplist.h"

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

enum node_type {
	//If a node's ever changed into N_SERVER, then it's a SVR
	//			   into N_CLOUD, then it's a CLD
	//			   otherwise it's a CLNT
	SVR, CLD, CLNT //Server cloud, client.
};

struct speed_rec {
	//Track inboud/outboud speed usage change
	double time;
	struct node *n;
	double speed;
	struct list_head srecs;
	struct skip_list_head osrecs;
	int dir;
};

struct node {
	uint32_t id;
	double ctime;
	enum node_type type;
	struct list_head srecs; //Speed change records
	int nsrec;
	UT_hash_handle hh;
};

static inline const char *
strntype(int type){
	switch(type){
		case SVR:
			return "Server";
		case CLD:
			return "Cloud";
		case CLNT:
			return "Client";
		default:
			return NULL;
	}
}

struct speed_rec *node_tracker(struct node **nhash, struct record *r){
	struct speed_rec *sr = NULL;
	struct node *n;
	if (r->major != 0)
		return sr;
	switch(r->minor){
		case R_NODE_STATE:
			HASH_FIND_INT(*nhash, &r->id, n);
			if (r->a.u8 == N_CLOUD)
				n->type = CLD;
			else if (r->a.u8 == N_SERVER)
				n->type = SVR;
			break;
		case R_NODE_CREATE:
			n = talloc(1, struct node);
			n->type = CLNT;
			INIT_LIST_HEAD(&n->srecs);
			n->id = r->id;
			n->ctime = r->time;
			HASH_ADD_INT(*nhash, id, n);
			break;
		case R_IN_USAGE:
		case R_OUT_USAGE:
			sr = talloc(1, struct speed_rec);
			sr->dir = r->minor&1;
			sr->speed = r->a.d;
			HASH_FIND_INT(*nhash, &r->id, n);
			sr->n = n;
			sr->time = r->time;
			list_add(&sr->srecs, &n->srecs);
			n->nsrec++;
			break;
	}
	return sr;
}

struct lnode {
	struct node *nh;
};

void *lnode_init(int argc, const char **argv){
	return talloc(1, struct lnode);
}

void lnode_next_record(void *d, struct record *r){
	struct lnode *x = d;
	node_tracker(&x->nh, r);
}

void lnode_finish(void *d){
	struct lnode *x = d;
	struct node *n, *tmp;
	HASH_ITER(hh, x->nh, n, tmp)
		printf("%u %s\n", n->id, strntype(n->type));
}

struct n1spd {
	int node_id;
	int dir;
	struct node *nh;
};

void *n1spd_init(int argc, const char **argv){
	if (argc != 2)
		return NULL;
	int id = atoi(argv[0]);
	struct n1spd *x = talloc(1, struct n1spd);
	if (strcmp(argv[1], "in") == 0)
		x->dir = 1;
	else if (strcmp(argv[1], "out") == 0)
		x->dir = 0;
	else
		return NULL;
	x->node_id = id;
	return x;
}

void n1spd_next_record(void *d, struct record *r){
	struct n1spd *x = d;
	node_tracker(&x->nh, r);
}

void n1spd_finish(void *d){
	struct n1spd *x = d;
	struct node *n = NULL;
	HASH_FIND_INT(x->nh, &x->node_id, n);
	assert(n);
	struct speed_rec *sr, *psr = NULL;
	printf("#Speed report for node %u, dir %d\n#Time\tSpeed\n",
	       x->node_id, x->dir);
	printf("%lf 0\n", n->ctime);
	list_for_each_entry_reverse(sr, &n->srecs, srecs){
		if (sr->dir != x->dir)
			continue;
		if (psr)
			printf("%lf %lf\n", sr->time, psr->speed);
		else
			printf("%lf 0\n", sr->time);
		printf("%lf %lf\n", sr->time, sr->speed);
		psr = sr;
	}
}

struct analyzer analyzer_table[] = {
	{"test", NULL, test_next_record, NULL},
	{"list_nodes", lnode_init, lnode_next_record, lnode_finish},
	{"single_node_speed", n1spd_init, n1spd_next_record, n1spd_finish},
	{NULL, NULL, NULL},
};
