#pragma once
#include "record.h"
extern struct analyzer {
	const char *name;
	void *(*init)(void);
	void (*next_record)(void *, struct record *);
	void (*finish)(void *);
}analyzer_table[];
