#include "skiplist.h"

#define LOG_DOMAIN "skiptest"

#include "log.h"
//A fuzzy test for skiplist
struct x{
	int key;
	struct skip_list_head h;
};
struct x *ht = NULL;
int global_log_level = LOG_DEBUG;
int cmp(struct skip_list_head *a, void *key){
	int k = *(int *)key;
	struct x *xx = skip_list_entry(a, struct x, h);
	return xx->key - k;
}
#define NK (random()%1000)
int main(){
	int count=20000, i;
	struct skip_list_head head;
	skip_list_init_head(&head);
	for(i=0; i<count; i++) {
		struct x *nx = calloc(1, sizeof(struct x));
		nx->key = NK;
		skip_list_insert(&head, &nx->h, &nx->key, cmp);
		log_debug("Inserting %d\n", nx->key);
		int dr = random()%3;
		if(dr == 2){
			int kkk = NK;
			struct skip_list_head *kk = skip_list_find(&head, &kkk, cmp);
			struct x *tx = skip_list_entry(kk, struct x, h);
			if(kk){
				log_debug("Finding %d, found %d, delete\n", kkk, tx->key);
				skip_list_delete(kk);
				struct x *tx = skip_list_entry(kk, struct x, h);
				free(tx);
			}else
				log_debug("Finding %d, not found\n", kkk);
		}else if(dr == 1){
			int kkk = NK;
			log_debug("Deleting %d by key...\n", kkk);
			struct skip_list_head *th = skip_list_extract_by_key(&head, &kkk, cmp);
			struct x *ltx = skip_list_entry(th, struct x, h);
			if(!th)
				log_debug(" ... not found\n");
			else {
				log_debug(" ... found %d\n", ltx->key);
				free(ltx);
			}
		}
	}

	while(!skip_list_empty(&head))
		skip_list_delete(head.next[0]);

	return 0;
}
