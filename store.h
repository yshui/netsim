#pragma once
#include "uthash.h"
#include "data.h"

static inline struct resource *
store_get(struct store *s, id_t resource_id){
	struct resource *r = NULL;
	HASH_FIND_INT(s->rsrc_hash, &resource_id, r);
	return r;
}

static inline void
store_delete(struct store *s, struct resource *r){
	HASH_DEL(s->rsrc_hash, r);
	s->total_size -= r->len;
}

static inline void
store_del(struct store *s, id_t resource_id){
	struct resource *r = NULL;
	HASH_FIND_INT(s->rsrc_hash, &resource_id, r);
	if (r)
		store_delete(s, r);
}

static inline int
store_set(struct store *s, struct resource *rsrc){
	struct resource *rr = NULL;
	HASH_FIND_INT(s->rsrc_hash, &rsrc->resource_id, rr);
	if (rr)
		return -1;
	HASH_ADD_INT(s->rsrc_hash, resource_id, rsrc);
	s->total_size += rsrc->len;
	return 0;
}
