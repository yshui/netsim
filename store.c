#include "uthash.h"
#include "data.h"

struct resource *
store_get(struct store *s, int resource_id){
	struct resource *r = NULL;
	HASH_FIND_INT(s->rsrc_hash, &resource_id, r);
	return r;
}
