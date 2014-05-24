#pragma once

#ifndef LOG_DOMAIN
# define LOG_DOMAIN "(missing log domain, file: " __BASE_FILE__ ") "
#endif
#define LOG_PREFIX (LOG_DOMAIN ": ")

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//Some loglevel definitions
#define LOG_EMERG	0
#define LOG_ALERT	1
#define LOG_CRIT	2
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	6
#define LOG_DEBUG	7

extern int global_log_level;

static inline void log_metav(int log_level, const char *fmt, va_list args){
	if (log_level > global_log_level)
		return;
	fprintf(stderr, "%s", LOG_PREFIX);
	vfprintf(stderr, fmt, args);
}

static inline void __attribute__ ((format(printf, 3, 4)))
log_meta(int log_level, const char *prefix, const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	log_metav(log_level, fmt, args);
}

#define log_debug(...) log_meta(LOG_DEBUG, LOG_PREFIX, __VA_ARGS__)
#define log_info(...) log_meta(LOG_INFO, LOG_PREFIX, __VA_ARGS__)
#define log_warning(...) log_meta(LOG_WARNING, LOG_PREFIX, __VA_ARGS__)
#define log_err(...) log_meta(LOG_ERR, LOG_PREFIX, __VA_ARGS__)
