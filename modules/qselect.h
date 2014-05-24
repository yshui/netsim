#pragma once
#include <stdlib.h>
#include <stdint.h>

#define def_qselect(name, type) \
static inline type * \
qselect_##name(type *v, size_t nmemb, size_t k) { \
	static type tmp; \
	int i, st; \
\
	for (st = i = 0; i < nmemb - 1; i++) { \
		if (cmp(v+i, v+nmemb-1) > 0) continue; \
		tmp = v[i]; \
		v[i] = v[st]; \
		v[st] = tmp; \
		st++; \
	} \
\
	tmp = v[st]; \
	v[st] = v[nmemb-1]; \
	v[nmemb-1] = tmp; \
\
	return k == st ? v+st : \
			 st > k ? qselect_##name(v, st, k) : \
				  qselect_##name(v+st, nmemb-st, k-st); \
}
