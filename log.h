#pragma once

#define _GNU_SOURCE

#ifndef LOG_DOMAIN
# define LOG_PREFIX "(missing log domain, file: " __BASE_FILE__ ") "
#else
# define LOG_PREFIX (LOG_DOMAIN ": ")
#endif

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
	char *buf;
	vasprintf(&buf, fmt, args);

	char *buf2 = (char *)malloc(strlen(buf)+strlen(LOG_PREFIX)+1);
	strcpy(buf2, LOG_PREFIX);
	strcpy(buf2+strlen(LOG_PREFIX), buf);

	fputs(buf2, stderr);
	free(buf);
	free(buf2);
}

static inline void __attribute__ ((format(printf, 2, 3)))
log_meta(int log_level, const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	log_metav(log_level, fmt, args);
}

#define log_debug(...) log_meta(LOG_DEBUG, __VA_ARGS__)
#define log_info(...) log_meta(LOG_INFO, __VA_ARGS__)
#define log_warning(...) log_meta(LOG_WARNING, __VA_ARGS__)
#define log_err(...) log_meta(LOG_ERR, __VA_ARGS__)
