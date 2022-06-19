#include "broiler/utils.h"

/* Same as preadv(2) execpt that this function never returns EAGAIN or EINTR */
ssize_t
broiler_pread(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
	ssize_t nr;

restart:
	nr = pread(fd, (void *)iov, iovcnt, offset);
	if ((nr < 0) && ((errno == EAGAIN) || (errno == EINTR)))
		goto restart;

	return nr;
}

/* Same as pwrite(2) except that this function never returns EAGAIN or EINTR */
ssize_t
broiler_pwrite(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
	ssize_t nr;

restart:
	nr = pwrite(fd, iov, iovcnt, offset);
	if ((nr < 0) && ((errno == EAGAIN) || (errno == EINTR)))
		goto restart;

	return nr;
}

/* Same as read(2) except that this function never returns EAGAIN or EINTR.  */
ssize_t
broiler_read(int fd, void *buf, size_t count)
{
	ssize_t nr;

restart:
	nr = read(fd, buf, count);
	if ((nr < 0) && ((errno == EAGAIN) || (errno == EINTR)))
		goto restart;

	return nr;
}

/* Same as write(2) except that this function never returns EAGAIN or EINTR */
ssize_t
broiler_write(int fd, const void *buf, size_t count)
{
	ssize_t nr;

restart:
	nr = write(fd, buf, count);
	if ((nr < 0) && ((errno == EAGAIN) || (errno == EINTR)))
		goto restart;

	return nr;
}

ssize_t read_in_full(int fd, void *buf, size_t count)
{
	ssize_t total = 0;
	char *p = buf;

	while (count > 0) {
		ssize_t nr;

		nr = broiler_read(fd, p, count);
		if (nr <= 0) {
			if (total > 0)
				return total;

			return -1;
		}
		count -= nr;
		total += nr;
		p += nr;
	}
	return total;
}