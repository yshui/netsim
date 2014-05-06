#pragma once

void write_record(uint8_t major, uint8_t minor, uint32_t node_id,
	   double time, int bytes, void *value, struct sim_state *s);

void open_record(const char *filename, int create, struct sim_state *s);
