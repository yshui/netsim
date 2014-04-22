#pragma once

#include <stdbool.h>

#include "list.h"
#include "skiplist.h"
#include "uthash.h"

struct sim_state;
struct flow;

struct pricing {
	double per_gb_bandwidth;
	double per_gb_disk;
	double per_hour;
};

struct range {
	int start, len, total_len;
	int grow; //grow speed, Kbits per second
	struct skip_list_head ranges;
	struct list_head *consumers;
	struct flow *producer;
};

struct resource {
	int resource_id, len;
	struct skip_list_head ranges;
	UT_hash_handle hh;
};

struct store {
	int total_size; //In Kbits
	struct resource *rsrc_hash;
};

struct connection {
	int upbound_bandwidth;
	int snd_spd, rcv_spd;
	struct node *src, *dst;
	struct flow *f; //Might be null
	struct list_head ins, outs;
	struct list_head *spd_evs;
};

struct node {
	int inbound, outbound;
	int inbound_usage, outbound_usage;
	void *loction, *activity; //Used for calculate bandwidth between nodes
	struct store *store;
	struct pricing *p; //Pricing infomation
	struct list_head *inbound_conn; //Nodes connected with this node
	struct list_head *outbound_conn;
	//Return true if the node decide to accept this request
	int node_id;
};

struct flow {
	//This is a directional flow
	struct node *src, *dst;
	int bandwidth;
	int resource_id;
	int start;
	double begin_time;
	//Done event is when the flow has filled the target range
	//Drain event is when the flow has drained the source range
	struct event *done, *drain;
	struct range *drng, *srng;
	struct list_head consumers;
};

enum event_type {
	FLOW_DRAIN,
	FLOW_DONE,
	FLOW_SOURCE_THROTTLE, //Throttle flow speed 'cause source can't keep up
	PACKET_DONE,
	SPEED_CHANGE,
	USER,
	LAST_EVENT,
};

typedef void (*event_handler)(struct event *);

struct event {
	enum event_type type;
	double time;
	void *data;
	event_handler eh;
	//struct sim_state *s;
	struct skip_list_head events;
};

struct sim_state {
	double now;
	struct skip_list_head events;
	struct resource *resources;
	struct node *nodes;
	struct list_head *flows;
	event_handler default_handler[LAST_EVENT];
	int (*bwcalc)(void *src, void *dst);
	int (*dlycalc)(void *src, void *dst);
	void (*evgen)(struct sim_state *s);
};


static inline
struct store *store_new(void){
	struct store *s = calloc(1, sizeof(struct store));
	return s;
}

static inline
struct event *event_new(double time, enum event_type t, void *d){
	struct event *e = calloc(1, sizeof(struct event));
	e->time = time;
	e->type = t;
	e->data = d;
	//e->s = NULL;
	return e;
}

static inline
struct range *range_new(int start){
	struct range *rng = calloc(1, sizeof(struct range));
	rng->start = start;
	rng->len = 0;
	return rng;
}

#define talloc(nmemb, type) (type *)calloc(nmemb, sizeof(type))
