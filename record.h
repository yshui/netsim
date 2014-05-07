#pragma once

#include <stdint.h>

#include "data.h"

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

void write_record(uint8_t major, uint8_t minor, uint32_t id,
	   int8_t bytes, void *value, struct sim_state *s);

void open_record(const char *filename, int create, struct sim_state *s);
