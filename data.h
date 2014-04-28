#pragma once

#include <stdbool.h>

#include "list.h"
#include "skiplist.h"
#include "uthash.h"
#include "common.h"

struct sim_state;
struct flow;

struct pricing {
	double per_gb_bandwidth;
	double per_gb_disk;
	double per_hour;
};

struct range {
	int start, len, total_len; //in Kbits
	double grow; //grow speed, Kbits per second
	double last_update;
	struct skip_list_head ranges;
	struct list_head consumers;
	struct flow *producer;
};

struct resource {
	int resource_id, len;
	struct skip_list_head ranges;
	UT_hash_handle hh;
};

struct store {
	int total_size; //In Kbits, enough for 4Tbits or 512GBytes
	struct resource *rsrc_hash;
};

enum peer_type {P_SND = 0, P_RCV};

struct connection {
	double bwupbound;
	double speed[2];
	double delay;
	//when will the last event sent by [dir] reach [!dir]
	double pending_event[2];
	//outbound/src/snd = [0], inbound/dst/rcv = [1]
	//{inbound,outbound}_max = sum of bwupbound
	//When {inbound,outbound}_max < {inbound,outbound}, all connections
	//gets their speed = bwupbound.
	//
	//Otherwise let:
	//speed share = bwupbound*{inbound,outbound}/{inbound,outbound}_max
	//Speed share is like QoS, limiting the maximum local send receive
	//speed, while bwupbound is the physical transfer speed limit on the
	//route path. And inbound, outbound is the physical local speed limit
	//on a given node.
	//initial speed is determined on flow creation, equals to the outbound
	//speed share of the connection on src side. When a speed is descreased
	//due to insufficient speed share on dst side or new connection created
	//on src side, the speed share to spread to other connections on
	//{src,dst} side (which decrease the speed share).
	//Obviously a speed share won't exceed bwupbound
	//
	//When a dst want to increase its rcv speed, it can increase above its
	//speed share on src side. src side should decrease those connections
	//whose speed exceed thier speed share to fulfill the request.
	//When src want to increase snd speed, the dst does the similar thing.
	//
	//So the maximum speed possible is min(snd_spd_share, rcv_spd_share).
	struct node *peer[2];
	struct flow *f; //Might be null
	struct list_head conns[2];
	struct list_head spd_evs;
};

struct node {
	double maximum_bandwidth[2];
	double bandwidth_usage[2];
	double total_bwupbound[2]; // sum of all bwupbound
	void *user_data; //Used for calculate bandwidth between nodes
	struct store *store;
	struct pricing *p; //Pricing infomation
	struct list_head conns[2]; //Nodes connected with this node
	//Return true if the node decide to accept this request
	int node_id;
};

struct flow {
	//This is a directional flow
	struct node *src, *dst;
	double bandwidth;
	int resource_id;
	int start;
	double begin_time;
	//Done event is when the flow has filled the target range
	//Drain event is when the flow has drained the source range
	struct event *done, *drain;
	struct range *drng, *srng;
	struct connection *c;
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

typedef void (*event_handler_func)(struct event *);

enum handler_priority {
	HNDR_DEFAULT = 0,
	HNDR_USER,
};

struct event_handler{
	event_handler_func f;
	struct list_head handlers;
	enum handler_priority pri;
};

struct event {
	enum event_type type;
	//time: when will the event be triggered
	//qtime: when is this event queued
	double time, qtime;
	void *data;
	//struct sim_state *s;
	struct skip_list_head events;
};

struct sim_state {
	double now;
	struct skip_list_head events;
	struct resource *resources;
	struct node *nodes;
	struct list_head flows;
	struct list_head handlers[LAST_EVENT];
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
