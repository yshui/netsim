#pragma once
#include "record_reader.h"
extern struct analyzer {
	const char *name;
	void *(*init)(int, const char **);
	void (*next_record)(void *, struct record *);
	void (*finish)(void *);
}analyzer_table[];
