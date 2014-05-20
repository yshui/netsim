#pragma once

#include <stdbool.h>

#include "list.h"
#include "skiplist.h"
#include "uthash.h"
#include "common.h"

struct sim_state;
struct flow;

typedef unsigned int id_t;

struct range {
	size_t start, len, total_len; //in Kbits
	double grow; //grow speed, Kbits per second
	double last_update;
	struct skip_list_head ranges;
	struct list_head consumers;
	struct flow *producer;
	struct node *owner;
};

struct resource {
	id_t resource_id;
	size_t len;
	int bit_rate;
	struct skip_list_head ranges;
	UT_hash_handle hh;
	struct node *owner;
};

struct store {
	int total_size; //In Kbits, enough for 4Tbits or 512GBytes
	struct resource *rsrc_hash;
};

enum peer_type {P_SND = 0, P_RCV};

struct connection {
	id_t conn_id;
	UT_hash_handle hh;
	double bwupbound;
	double speed[2];
	double delay;
	//when will the last event sent by [dir] reach [!dir]
	double pending_event[2];
	int closing;
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

enum node_state {
	N_OFFLINE,
	N_STALE,
	N_PLAYING,
	N_DONE,
	N_IDLE,
};

enum ue_type {
	DONE_PLAY,
	PAUSE_BUFFERING,
	DONE_BUFFERING,
};

struct user_event {
	int type;
	struct def_user *d;
	void *data;
};

struct def_user {
	double bit_rate;
	enum node_state next_state;
	void *trigger;
	//low water mark: stop playing
	//high water mark: start playing
	int highwm, lowwm;
	int buffer_pos;
	int resource;
	double last_update, last_speed;
	struct node *n;
	struct event *e;
};

struct node {
	double maximum_bandwidth[2];
	double bandwidth_usage[2];
	double total_bwupbound[2]; // sum of all bwupbound
	void *user_data; //Used for calculate bandwidth between nodes
	struct store *store;
	struct list_head conns[2]; //Nodes connected with this node
	//Return true if the node decide to accept this request
	id_t node_id;
	UT_hash_handle hh;
	enum node_state state;
};

struct flow {
	//This is a directional flow
	struct node *src, *dst;
	double bandwidth;
	int resource_id;
	int start;
	double begin_time;
	id_t flow_id;
	UT_hash_handle hh;
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
	FLOW_SPEED_THROTTLE, //Throttle flow speed 'cause source can't keep up
	PACKET_DONE,
	SPEED_CHANGE,
	USER,
	LAST_EVENT,
};

typedef void (*event_handler_func)(struct event *, struct sim_state *);

enum handler_priority {
	HNDR_DEFAULT = 0,
	HNDR_USER,
	HNDR_CLEANER, //Free spaces and stuff
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
	bool active;
	void *data;
	//struct sim_state *s;
	struct skip_list_head events;
};

#define SND 0
#define RCV 1
#define R_BANDWIDTH_USAGE 0
#define R_SPEED_CHANGE 2

struct sim_state {
	//Time keeper:
	double now;
	struct skip_list_head events;
	struct list_head handlers[LAST_EVENT];

	//Hashes:
	struct node *nodes;
	struct flow *flows;
	struct connection *conns;

	//Record:
	void *record_tail, *record_head;
	int record_file_size, record_fd;

	//User provided functions:
	double (*bwcalc)(void *src, void *dst);
	double (*dlycalc)(void *src, void *dst);
};


static inline
struct store *store_new(void){
	struct store *s = talloc(1, struct store);
	return s;
}

static inline
struct event *event_new(double time, enum event_type t, void *d){
	struct event *e = talloc(1, struct event);
	e->time = time;
	e->type = t;
	e->data = d;
	e->active = false;
	//e->s = NULL;
	return e;
}

static inline void event_free(struct event *e){
	if (!e)
		return;
	free(e);
}

static inline
struct range *range_new(size_t start, size_t len){
	struct range *rng = talloc(1, struct range);
	rng->start = start;
	rng->len = len;
	INIT_LIST_HEAD(&rng->consumers);
	return rng;
}

static inline
struct sim_state *sim_state_new(void){
	int i;
	struct sim_state *s = talloc(1, struct sim_state);
	skip_list_init_head(&s->events);
	s->now = 0;
	for(i = 0; i < LAST_EVENT; i++)
		INIT_LIST_HEAD(&s->handlers[i]);
	return s;
}

static inline
struct node *node_new(void){
	struct node *n = talloc(1, struct node);

	n->total_bwupbound[0] = n->total_bwupbound[1] = 0;
	n->state = N_OFFLINE;
	n->maximum_bandwidth[0] = n->maximum_bandwidth[1] = 0;
	INIT_LIST_HEAD(&n->conns[0]);
	INIT_LIST_HEAD(&n->conns[1]);
	n->bandwidth_usage[0] = n->bandwidth_usage[1] = 0;
	n->store = store_new();

	return n;
}

static inline
struct resource *resource_new(id_t id, size_t s){
	struct resource *r = talloc(1, struct resource);
	skip_list_init_head(&r->ranges);
	r->len = s;
	r->resource_id = id;
	return r;
}

static inline
const char *strstate(int state){
	switch(state){
		case N_DONE:
			return "Done";
		case N_PLAYING:
			return "Playing";
		case N_IDLE:
			return "Idle";
		case N_STALE:
			return "Stale";
		case N_OFFLINE:
			return "Offline";
	}
	return NULL;
}
