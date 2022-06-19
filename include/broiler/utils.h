#ifndef _BISCUITOS_UTILS_H
#define _BISCUITOS_UTILS_H

#include "broiler/broiler.h"

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

extern ssize_t broiler_pread(int, const struct iovec *, int, off_t);
extern ssize_t broiler_pwrite(int, const struct iovec *, int, off_t);
extern ssize_t read_in_full(int fd, void *buf, size_t count);

#endif
