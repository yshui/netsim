#define _GNU_SOURCE

#include <stdint.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define LOG_DOMAIN "stat"

#include "log.h"
#include "data.h"

struct record_disk{
	uint8_t major;
	uint8_t minor;
	uint32_t node_id;
	uint32_t time;
	uint16_t utime;
};

void write_record(uint8_t major, uint8_t minor, uint32_t node_id,
		  int bytes, void *value, struct sim_state *s){
	struct record_disk *r = s->record_tail;
	assert(s->record_tail);

	uint8_t *new_tail = ((uint8_t *)s->record_tail)+sizeof(*r)+6;
	if (s->record_file_size+s->record_head <= (void *)new_tail) {
		size_t old_len = s->record_tail-s->record_head;
		int ret = ftruncate(s->record_fd, s->record_file_size<<1);

		if (ret != 0)
			log_err("Can't enlarge stat file.");
		else{
			s->record_head = mremap(s->record_head, s->record_file_size,
						s->record_file_size<<1, MREMAP_MAYMOVE);
			s->record_tail = ((uint8_t *)s->record_head)+old_len;
			r = s->record_tail;
			s->record_file_size <<= 1;
		}
	}

	r->major = major;
	r->minor = minor;
	r->node_id = htonl(node_id);
	r->time = htonl((int)s->now);

	uint16_t utime = (s->now-((int)s->now))*1000;
	r->utime = htons(utime);

	union {
		uint8_t *u8p;
		uint16_t *u16p;
		uint32_t *u32p;
	}a;
	double tmp;
	a.u8p = s->record_tail+sizeof(*r);

	switch(bytes){
		case 1:
			*(a.u8p) = *(uint8_t *)value;
			break;
		case 2:
			*(a.u16p) = htons(*(uint16_t *)value);
			break;
		case 4:
			*(a.u32p) = htonl(*(uint32_t *)value);
			break;
		case -1:
			tmp = *(double *)value;
			assert(tmp < 4.2e9);
			*(a.u32p) = htonl((int)tmp);
			a.u32p++;
			tmp = tmp-((int)tmp);
			*(a.u16p) = htons(tmp*1000);
			break;
		default:
			assert(false);
	}

	uint32_t n = *(uint32_t *)s->record_head;
	n = ntohl(n);
	n += sizeof(*r);
	if (bytes != -1)
		n += bytes;
	else
		n += 6;

	*(uint32_t *)s->record_head = htonl(n);
}

void open_record(const char *filename, int create, struct sim_state *s){
	if (s->record_head) {
		assert(s->record_tail);
		return;
	}

	int fd = open(filename, O_RDWR);
	if (fd < 0) {
		if (errno == ENOENT && create) {
			fd = open(filename, O_RDWR|O_CREAT|O_EXCL);
			if (fd < 0) {
				log_err("Create stat file failed.");
				return;
			}
			if(ftruncate(fd, 1024*1024)) {
				log_err("Set stat file initial size failed.");
				return;
			}
		}else{
			log_err("Stat file doesn't exist.");
			return;
		}
	}

	struct stat buf;
	int ret = fstat(fd, &buf);

	s->record_file_size = buf.st_size;
	s->record_head = mmap(NULL, buf.st_size, PROT_READ|PROT_WRITE,
			      MAP_SHARED, fd, 0);
	uint32_t n = *(uint32_t *)s->record_head;
	n = ntohl(n);
	s->record_tail = ((uint8_t *)s->record_head)+4+n;
	s->record_fd = fd;
}
