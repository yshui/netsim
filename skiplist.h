#pragma once

#include <stdlib.h>
#include <string.h>

#include "common.h"

#define MAX_HEIGHT 32

struct skip_list_head {
	struct skip_list_head **next, *prev;
	int h;
};

#define skip_list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define skip_list_empty(ptr) \
	((ptr)->next == NULL || (ptr)->next[0] == NULL)

static inline void skip_list_init_head(struct skip_list_head *h){
	h->h = MAX_HEIGHT;
	h->next = calloc(MAX_HEIGHT, sizeof(void *));
	h->prev = NULL;
}

static inline int skip_list_gen_height(void){
	int r = random();
	int h = 1;
	for(;r&1;r>>=1)
		h++;
	return h;
}

static inline void skip_list_init_node(struct skip_list_head *n){
	int newh = skip_list_gen_height();
	if (newh > n->h)
		n->next = realloc(n->next, sizeof(void *)*newh);
	n->h = newh;
	memset(n->next, 0, sizeof(void *)*newh);
}

typedef int (*skip_list_cmp)(struct skip_list_head *a, void *key);

static int skip_list_last_cmp(struct skip_list_head *a, void *key){
	return a ? -1 : 1;
}

static inline void skip_list_previous(struct skip_list_head *head,
				      void *key, skip_list_cmp cmp,
				      struct skip_list_head **res){
	int h = head->h-1;
	struct skip_list_head *n = head;
	while(1){
		while(h >= 0 &&
		     (n->next[h] == NULL || cmp(n->next[h], key) >= 0)){
			res[h] = n;
			h--;
		}
		if (h < 0)
			break;
		while(n->next[h] && cmp(n->next[h], key) < 0)
			n = n->next[h];
	}
}

static inline void
skip_list_insert(struct skip_list_head *h, struct skip_list_head *n,
		      void *key, skip_list_cmp cmp) {
	skip_list_init_node(n);
	int i;
	struct skip_list_head *hs[MAX_HEIGHT];
	skip_list_previous(h, key, cmp, hs);
	for(i = n->h-1; i >= 0; i--) {
		n->next[i] = hs[i]->next[i];
		hs[i]->next[i] = n;
	}
	if (n->next[0])
		n->next[0]->prev = n;
	n->prev = hs[0];
}

//Find the smallest element that is greater or equal to key.
static inline struct skip_list_head *
skip_list_find(struct skip_list_head *h, void *key, skip_list_cmp cmp){
	int i;
	struct skip_list_head *hs[MAX_HEIGHT];
	skip_list_previous(h, key, cmp, hs);
	return hs[0]->next[0];
}

static inline int
skip_list_delete(struct skip_list_head *h, void *key, skip_list_cmp cmp){
	int i;
	struct skip_list_head *hs[MAX_HEIGHT];
	skip_list_previous(h, key, cmp, hs);
	struct skip_list_head *node = hs[0]->next[0];
	if (!node || cmp(node, key) != 0)
		return -1;
	for(i = node->h-1; i >= 0; i--)
		hs[i]->next[i] = node->next[i];
	node->next[0]->prev = node->prev;
	free(node->next);
	node->h = 0;
	return 0;
}


static inline void
skip_list_delete_next(struct skip_list_head *h){
	int i;
	struct skip_list_head *next = h->next[0];
	for(i = next->h-1; i >= 0; i--)
		h->next[i] = next->next[i];
	if (next->next[0])
		next->next[0]->prev = h;
}

static inline void
skip_list_node_free(struct skip_list_head *n){
	free(n->next);
	n->h = 0;
}
