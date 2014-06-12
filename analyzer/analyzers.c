#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

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
	struct list_head srecs[2], osrecs;
	int dir;
};

struct node {
	uint32_t id;
	double ctime;
	enum node_type type;
	struct list_head srecs[2]; //Speed change records
	int nsrec;
	int tmp_state;
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
			INIT_LIST_HEAD(&n->srecs[0]);
			INIT_LIST_HEAD(&n->srecs[1]);
			n->id = r->id;
			n->ctime = r->time;
			n->tmp_state = -1;
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
			list_add(&sr->srecs[sr->dir], &n->srecs[sr->dir]);
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

void print_speed(struct list_head *h, int dir){
	struct speed_rec *sr, *psr = NULL;
	list_for_each_entry_reverse(sr, h, srecs[dir]){
		if (psr)
			printf("%lf %lf\n", sr->time, psr->speed);
		else
			printf("%lf 0\n", sr->time);
		printf("%lf %lf\n", sr->time, sr->speed);
		psr = sr;
	}
}

void print_speed_per(struct list_head *h, int dir, int period){
	struct speed_rec *sr, *psr = NULL;
	double last_time = 0, current = 0, left = period, last_speed = 0;
	int count = 0;
	list_for_each_entry_reverse(sr, h, srecs[dir]){
		double delta = sr->time - last_time;
		if (delta > left){
			current += last_speed*left/period;
			printf("%d %lf\n", count, current);
			current = 0;
			count++;
			delta -= left;
			while(delta>period){
				printf("%d %lf\n", count, last_speed);
				count++;
				delta -= period;
			}
			current = last_speed*delta/period;
			left = period-delta;
		}else{
			current += last_speed*delta/period;
			left -= delta;
		}
		last_time = sr->time;
		last_speed = sr->speed;
	}
	printf("%d %lf\n", count, current);
}


struct n1spd {
	int node_id;
	int dir;
	bool per_hour;
	struct node *nh;
};

void *n1spd_init(int argc, const char **argv){
	struct n1spd *x = talloc(1, struct n1spd);
	int id = -1;
	int i;
	for(i = 0; argv[i]; i++){
		if (strcmp(argv[i], "in") == 0)
			x->dir = 1;
		else if (strcmp(argv[i], "out") == 0)
			x->dir = 0;
		else if (strcmp(argv[i], "-h") == 0)
			x->per_hour = true;
		else
			id = atoi(argv[i]);
	}
	if (id < 0)
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
	if (!x->per_hour) {
		printf("#Speed report for node %u, dir %d\n#Time\tSpeed\n",
				n->id, x->dir);
		printf("%lf 0\n", n->ctime);
		print_speed(&n->srecs[x->dir], x->dir);
	}else
		print_speed_per(&n->srecs[x->dir], x->dir, 60*60);
}

struct ntspd {
	int dir;
	int type;
	bool per_hour;
	enum node_type nt;
	struct node *nh;
	struct list_head osrecs;
};

void *ntspd_init(int argc, const char **argv){
	struct ntspd *x = talloc(1, struct ntspd);
	INIT_LIST_HEAD(&x->osrecs);
	int id = -1;
	int i;
	for(i = 0; argv[i]; i++){
		if (strcmp(argv[i], "in") == 0)
			x->dir = 1;
		else if (strcmp(argv[i], "out") == 0)
			x->dir = 0;
		else if (strcmp(argv[i], "-h") == 0)
			x->per_hour = true;
		else if (strcmp(argv[i], "server") == 0)
			x->nt = SVR;
		else if (strcmp(argv[i], "client") == 0)
			x->nt = CLNT;
		else if (strcmp(argv[i], "cloud") == 0)
			x->nt = CLD;
		else
			return NULL;
	}
	return x;
}

void ntspd_next_record(void *d, struct record *r){
	struct ntspd *x = d;
	struct speed_rec *sr = node_tracker(&x->nh, r);
	if (sr && sr->dir == x->dir)
		list_add(&sr->osrecs, &x->osrecs);
}

void ntspd_finish(void *d){
	struct ntspd *x = d;
	struct speed_rec *sr;
	double speed = 0;
	struct list_head h;
	INIT_LIST_HEAD(&h);
	list_for_each_entry_reverse(sr, &x->osrecs, osrecs){
		if (sr->n->type != x->nt)
			continue;
		//The order of speed records is reversed, so use .next
		struct speed_rec *psr =
			list_entry(sr->srecs[x->dir].next, struct speed_rec,
				   srecs[x->dir]);
		if (sr->srecs[x->dir].next != &sr->n->srecs[x->dir])
			speed -= psr->speed;
		speed += sr->speed;
		struct speed_rec *nsr = talloc(1, struct speed_rec);
		nsr->speed = speed;
		nsr->time = sr->time;
		list_add(&nsr->srecs[x->dir], &h);
	}
	if (!x->per_hour){
		printf("0 0\n");
		print_speed(&h, x->dir);
	}else
		print_speed_per(&h, x->dir, 60*60);
}

struct state_record {
	int new_state;
	double time;
	struct node *n;
	struct list_head srs;
};

struct cldol {
	struct node *nh;
	struct list_head srs;
};

void *cldol_init(int argc, const char **argv){
	struct cldol *d = talloc(1, struct cldol);
	INIT_LIST_HEAD(&d->srs);
	return d;
}

void cldol_next_record(void *d, struct record *r){
	struct cldol *x = d;
	node_tracker(&x->nh, r);
	if (r->major == 0 && r->minor == R_NODE_STATE) {
		struct state_record *sr = talloc(1, struct state_record);
		struct node *n;
		HASH_FIND_INT(x->nh, &r->id, n);
		assert(n);
		sr->n = n;
		sr->time = r->time;
		sr->new_state = r->a.u8;
		list_add(&sr->srs, &x->srs);
	}
}

#define eps (1e-6)

void cldol_finish(void *d){
	struct state_record *sr;
	struct cldol *x = d;
	int online = 0;
	double last_time = 0;
	bool changed = false;
	list_for_each_entry_reverse(sr, &x->srs, srs){
		if (sr->n->type != CLD)
			continue;
		if (sr->n->tmp_state < 0) {
			if (sr->new_state == N_CLOUD) {
				online++;
				changed = true;
			}
		}else if (sr->n->tmp_state == N_DYING) {
			assert(sr->new_state == N_OFFLINE ||
			       sr->new_state == N_CLOUD);
			if (sr->new_state == N_OFFLINE) {
				online--;
				changed = true;
			}
		}else if (sr->n->tmp_state == N_OFFLINE) {
			assert(sr->new_state == N_CLOUD);
			if (sr->new_state == N_CLOUD) {
				online++;
				changed = true;
			}
		}else if (sr->n->tmp_state == N_CLOUD) {
			assert(sr->new_state == N_OFFLINE ||
			       sr->new_state == N_DYING);
			if (sr->new_state == N_OFFLINE) {
				online--;
				changed = true;
			}
		}
//		fprintf(stderr, "%s %s\n", strstate(sr->n->tmp_state), strstate(sr->new_state));
		sr->n->tmp_state = sr->new_state;
		if (changed && sr->time > last_time+eps) {
			printf("%lf %d\n", sr->time, online);
			changed = false;
		}
		last_time = sr->time;
	}
}

struct analyzer analyzer_table[] = {
	{"test", NULL, test_next_record, NULL},
	{"list_nodes", lnode_init, lnode_next_record, lnode_finish},
	{"single_node_speed", n1spd_init, n1spd_next_record, n1spd_finish},
	{"node_type_speed", ntspd_init, ntspd_next_record, ntspd_finish},
	{"online_cloud", cldol_init, cldol_next_record, cldol_finish},
	{NULL, NULL, NULL},
};
