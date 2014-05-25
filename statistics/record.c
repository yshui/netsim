#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

#include "record.h"
struct record_handle *
open_record(const char *filename){
	struct record_handle *rh = talloc(1, struct record_handle);
	rh->fd = open(filename, O_RDONLY);
	if (rh->fd < 0){
		free(rh);
		return NULL;
	}
	rh->head = mmap(NULL, 16, PROT_READ, MAP_PRIVATE, rh->fd, 0);
	rh->len = *(uint32_t *)rh->head;
	rh->len = ntohl(rh->len);
	rh->head = mremap(rh->head, 16, rh->len+4, MREMAP_MAYMOVE);
	rh->tail = rh->head+4;

	return rh;
}

struct record *
read_record(struct record_handle *rh){
	if (rh->tail >= rh->head+4+rh->len)
		return NULL;
	struct record *r = talloc(1, struct record);
	uint8_t *ptr = rh->tail;
	r->major = *ptr;
	ptr++;
	r->minor = *ptr;
	ptr++;
	r->id = ntohl(*(uint32_t *)ptr);
	ptr += 4;
	r->time = ntohl(*(uint32_t *)ptr);
	ptr += 4;
	r->time += ((double)ntohs(*(uint16_t *)ptr))/1000.0;
	ptr += 2;
	int8_t bytes = *(int8_t *)ptr;
	ptr++;
	uint16_t tmp16 = 0;
	switch(bytes){
		case 1:
			r->type = U8;
			r->a.u8 = *ptr;
			ptr++;
			break;
		case 2:
			r->type = U16;
			r->a.u16 = ntohs(*(uint16_t *)ptr);
			ptr+=2;
			break;
		case 4:
			r->type = U32;
			r->a.u32 = ntohl(*(uint32_t *)ptr);
			ptr+=4;
			break;
		case -1:
			r->type = DOUBLE;
			r->a.d = ntohl(*(uint32_t *)ptr);
			ptr+=4;
			tmp16 = ntohs(*(uint16_t *)ptr);
			ptr+=2;
			r->a.d += ((double)tmp16)/1000.0;
			break;
		default:
			fprintf(stderr, "Corrupted record file\n");
			return NULL;
	}
	rh->tail = ptr;
	return r;
}
