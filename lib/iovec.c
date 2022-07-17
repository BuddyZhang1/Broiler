// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"

/*
 * memcpy_fromiovec_safe - Copy at most @len bytes from iovec to buffer.
 *                         Returns the remaining len.
 *
 * Note: this modifies the original iovec, the iov pointer, and the
 * iovcount to describe the remaining buffer.
 */
ssize_t memcpy_fromiovec_safe(void *buf, struct iovec **iov,
					size_t len, size_t *iovcount)
{
	size_t copy;

	while (len && *iovcount) {
		copy = min(len, (*iov)->iov_len);
		memcpy(buf, (*iov)->iov_base, copy);
		buf += copy;
		len -= copy;

		/* Move iov cursor */
		(*iov)->iov_base += copy;
		(*iov)->iov_len += copy;

		if (!(*iov)->iov_len) {
			(*iov)++;
			(*iovcount)--;
		}
	}

	return len;
}

/*
 * memcpy_toiovec - Copy kernel to iovec. Reutrns -EFAULT on error.
 * Not: this modifies the orignal iovec.
 */
int memcpy_toiovec(struct iovec *iov, unsigned char *kdata, int len)
{
	while (len > 0) {
		if (iov->iov_len) {
			int copy = min_t(unsigned int, iov->iov_len, len);

			memcpy(iov->iov_base, kdata, copy);
			kdata += copy;
			len -= copy;
			iov->iov_len -= copy;
			iov->iov_base += copy;
		}
		iov++;
	}

	return 0;
}
