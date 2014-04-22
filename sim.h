#pragma once

struct packet{
	struct node *src, *dst;
	void *data;
	int len;
};
