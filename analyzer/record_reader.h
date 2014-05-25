#pragma once
#include <stdlib.h>
#include <stdint.h>

#include "common.h"
#define R_USAGE 0
#define R_OUT_USAGE 0
#define R_IN_USAGE 1
#define R_SPD 2
#define R_SND_SPD 2
#define R_RCV_SPD 3
#define R_CONN_CREATE 4
#define R_CONN_DST 5
#define R_CONN_CLOSE 6
#define R_NODE_STATE 7
#define R_NODE_CREATE 8

struct record_handle {
	int fd;
	uint32_t len;
	void *head, *tail;
};

enum record_type {
	U8, U16, U32,
	DOUBLE,
};

struct record {
	uint8_t major;
	uint8_t minor;
	uint32_t id;
	double time;
	enum record_type type;
	union {
		uint8_t u8;
		uint16_t u16;
		uint32_t u32;
		double d;
	}a;
};

static inline const char *
strrtype(int rtype){
	switch(rtype){
		case R_OUT_USAGE:
			return "Outbound usage change";
		case R_IN_USAGE:
			return "Inbound usage change";
		case R_SND_SPD:
			return "Send speed change";
		case R_RCV_SPD:
			return "Receive speed change";
		case R_CONN_CREATE:
			return "Flow created";
		case R_CONN_DST:
			return "Flow change destination";
		case R_CONN_CLOSE:
			return "Flow closed";
		case R_NODE_STATE:
			return "Node state change";
		case R_NODE_CREATE:
			return "New node";
	}
	return NULL;
}

enum node_state {
	N_OFFLINE,
	N_STALE,
	N_PLAYING,
	N_DONE,
	N_IDLE,
	N_SERVER,
	N_CLOUD,
};

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
		case N_CLOUD:
			return "Cloud";
		case N_SERVER:
			return "Server";
	}
	return NULL;
}

struct record_handle *
open_record(const char *filename);

struct record *
read_record(struct record_handle *rh);

