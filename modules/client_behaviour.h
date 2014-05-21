#pragma once
#include "data.h"

enum ue_type {
	DONE_PLAY,
	PAUSE_BUFFERING,
	DONE_BUFFERING,
	NEW_CONNECTION,
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

void client_next_state_from_event(struct event *e, struct sim_state *s);
void client_handle_next_state(struct node *n, struct sim_state *s);
int client_new_connection(id_t rid, size_t start, struct node *server,
			  struct node *client, struct sim_state *s);
void client_start_play(struct node *client, id_t rid, struct sim_state *s);
void client_done(struct event *e, struct sim_state *s);
void client_speed_change(struct event *e, struct sim_state *s);
void client_lowwm_event(struct range *rng, struct sim_state *s);
void client_highwm_event(struct range *rng, struct sim_state *s);

