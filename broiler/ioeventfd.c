#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>

#include "broiler/broiler.h"
#include "broiler/kvm.h"
#include "broiler/ioeventfd.h"

#define IOEVENTFD_MAX_EVENTS	20

static struct epoll_event events[IOEVENTFD_MAX_EVENTS];
static bool ioeventfd_avail;
static int epoll_fd, epoll_stop_fd;

static void *ioeventfd_thread(void *param)
{
	u64 tmp = 1;

	prctl(PR_SET_NAME, "BiscuitOS-ioeventfd-worker");

	for (;;) {
		int nfds, i;

		nfds = epoll_wait(epoll_fd, events, IOEVENTFD_MAX_EVENTS, -1);
		for (i = 0; i < nfds; i++) {
			struct ioevent *ioevent;

			if (events[i].data.fd == epoll_stop_fd)
				goto done;

			ioevent = events[i].data.ptr;

			if (read(ioevent->fd, &tmp, sizeof(tmp)) < 0) {
				printf("Failed reading event.\n");
				exit(-1);
			}
			ioevent->fn(ioevent->broiler, ioevent->fn_ptr);
		}
	}

done:
	tmp = write(epoll_stop_fd, &tmp, sizeof(tmp));

	return NULL;
}

static int ioeventfd_start(void)
{
	pthread_t thread;

	if (!ioeventfd_avail)
		return -ENOSYS;

	return pthread_create(&thread, NULL, ioeventfd_thread, NULL);
}

int ioeventfd_init(struct broiler *broiler)
{
	struct epoll_event epoll_event = { .events = EPOLLIN };
	int r;

	ioeventfd_avail = kvm_support_extension(broiler, KVM_CAP_IOEVENTFD);
	if (!ioeventfd_avail)
		return 1; /* Not fatal, but let caller determine no-go. */

	epoll_fd = epoll_create(IOEVENTFD_MAX_EVENTS);
	if (epoll_fd < 0)
		return -errno;

	epoll_stop_fd = eventfd(0, 0);
	epoll_event.data.fd = epoll_stop_fd;

	r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, epoll_stop_fd, &epoll_event);
	if (r < 0)
		goto err_epoll_ctl;

	r = ioeventfd_start();
	if (r < 0)
		goto err_epoll_start;

	return 0;

err_epoll_start:
err_epoll_ctl:
	close(epoll_stop_fd);
	close(epoll_fd);

	return r;
}

int ioeventfd_exit(struct broiler *broiler)
{
	u64 tmp = 1;
	int r;

	if (!ioeventfd_avail)
		return 0;

	r = write(epoll_stop_fd, &tmp, sizeof(tmp));
	if (r < 0)
		return r;

	r = read(epoll_stop_fd, &tmp, sizeof(tmp));
	if (r < 0)
		return r;

	close(epoll_fd);
	close(epoll_stop_fd);

	return 0;
}
