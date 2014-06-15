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

//Note this struct is just used as a reference
struct _record_ondisk_format{
	uint8_t major;
	uint8_t minor;
	uint32_t id;
	uint32_t time;
	uint16_t utime;
	int8_t bytes;
};

void write_record(uint8_t major, uint8_t minor, uint32_t id,
		  int8_t bytes, void *value, struct sim_state *s){
	if (!s->record_head) {
		log_debug("No record file opened, ignore record.\n");
		return;
	}

	uint8_t *r = s->record_tail;
	assert(s->record_tail);

	uint8_t *new_tail = ((uint8_t *)s->record_tail)+1+1+4+4+2+1+6;
	if (s->record_file_size+s->record_head <= (void *)new_tail) {
		size_t old_len = s->record_tail-s->record_head;
		int ret = ftruncate64(s->record_fd, s->record_file_size<<1);

		if (ret != 0)
			log_err("Can't enlarge stat file: %s", strerror(errno));
		else{
			s->record_head = mremap(s->record_head, s->record_file_size,
						s->record_file_size<<1, MREMAP_MAYMOVE);
			s->record_tail = ((uint8_t *)s->record_head)+old_len;
			r = s->record_tail;
			s->record_file_size <<= 1;
		}
	}

	//Major
	*r = major;
	r++;
	//Minor
	*r = minor;
	r++;
	//Id
	*(uint32_t *)r = htonl(id);
	r += 4;
	//Decimal part of time
	//Store time as uint32_t pose a limit on time
	//Which is about 60 years, which won't be a problem
	//for simulation propose.
	*(uint32_t *)r = htonl((int)s->now);
	r += 4;

	uint16_t utime = (s->now-((int)s->now))*1000;
	//Usecs
	*(uint16_t *)r = htons(utime);
	r += 2;

	*(int8_t *)r = bytes;
	r++;

	union {
		uint8_t *u8p;
		uint16_t *u16p;
		uint32_t *u32p;
	}a;
	double tmp;
	a.u8p = r;

	switch(bytes){
		case 1:
			*(a.u8p) = *(uint8_t *)value;
			a.u8p++;
			break;
		case 2:
			*(a.u16p) = htons(*(uint16_t *)value);
			a.u16p++;
			break;
		case 4:
			*(a.u32p) = htonl(*(uint32_t *)value);
			a.u32p++;
			break;
		case -1:
			tmp = *(double *)value;
			assert(tmp < 4.2e9);
			*(a.u32p) = htonl((int)tmp);
			a.u32p++;
			tmp = tmp-((int)tmp);
			*(a.u16p) = htons(tmp*1000);
			a.u16p++;
			break;
		default:
			assert(false);
	}

	uint32_t n = *(uint32_t *)s->record_head;
	n = ntohl(n);
	n += a.u8p-(uint8_t *)s->record_tail;

	*(uint32_t *)s->record_head = htonl(n);
	s->record_tail = a.u8p;
}

void open_record(const char *filename, int create, struct sim_state *s){
	if (s->record_head) {
		assert(s->record_tail);
		return;
	}

	int fd = open(filename, O_RDWR, 0644);
	if (fd < 0) {
		if (errno == ENOENT && create) {
			fd = open(filename, O_RDWR|O_CREAT|O_EXCL, 0644);
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
