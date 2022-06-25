#ifndef _BROILER_IOVEC_H
#define _BROILER_IOVEC_H

extern ssize_t memcpy_fromiovec_safe(void *buf, struct iovec **iov,
                                        size_t len, size_t *iovcount);
extern int memcpy_toiovec(struct iovec *iov, unsigned char *kdata, int len);

static inline size_t iov_size(const struct iovec *iovec, size_t len)
{
	size_t size = 0, i;

	for (i = 0; i < len; i++)
		size += iovec[i].iov_len;

	return size;
}

#endif
