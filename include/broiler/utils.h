// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_UTILS_H
#define _BROILER_UTILS_H

#include "broiler/broiler.h"

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#else
#define NORETURN
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif

static inline ssize_t get_iov_size(const struct iovec *iov, int iovcnt)
{
	size_t size = 0;

	while (iovcnt--)
		size += (iov++)->iov_len;

	return size;
}

static inline void shift_iovec(const struct iovec **iov, int *iovcnt,
		size_t nr, ssize_t *total, size_t *count, off_t *offset)
{
	while (nr >= (*iov)->iov_len) {
		nr -= (*iov)->iov_len;
		*total += (*iov)->iov_len;
		*count -= (*iov)->iov_len;
		if (offset)
			*offset += (*iov)->iov_len;
		(*iovcnt)--;
		(*iov)++;
	}
}

/*
 * Find last (most significant) bit set. Same implementation as Linux:
 * fls(0) = 0, fls(1) = 1, fls(1UL << 63) = 64
 */
static inline int fls_long(unsigned long x)
{
	return x ? sizeof(x) * 8 - __builtin_clzl(x) : 0;
}


extern ssize_t broiler_pread(int, const struct iovec *, int, off_t);
extern ssize_t broiler_pwrite(int, const struct iovec *, int, off_t);
extern ssize_t read_in_full(int fd, void *buf, size_t count);
extern ssize_t read_file(int fd, char *buf, size_t max_size);
extern void die(const char *err, ...);
extern void die_perror(const char *s);
extern size_t strlcpy(char *dest, const char *src, size_t size);

#endif
