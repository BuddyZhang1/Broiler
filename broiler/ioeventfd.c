// SPDX-License-Identifier: GPL-2.0-only
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

static struct epoll_event broiler_events[IOEVENTFD_MAX_EVENTS];
static bool ioeventfd_avail;
static int epoll_fd, epoll_stop_fd;
static LIST_HEAD(used_ioevents);

static void *ioeventfd_thread(void *param)
{
	u64 tmp = 1;

	prctl(PR_SET_NAME, "Broiler-ioeventfd-worker");

	for (;;) {
		int nfds, i;

		nfds = epoll_wait(epoll_fd, broiler_events,
						IOEVENTFD_MAX_EVENTS, -1);
		for (i = 0; i < nfds; i++) {
			struct ioevent *ioevent;

			if (broiler_events[i].data.fd == epoll_stop_fd)
				goto done;

			ioevent = broiler_events[i].data.ptr;

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

int ioeventfd_add_event(struct ioevent *ioevent, int flags)
{
	struct kvm_ioeventfd kvm_ioevent;
	struct epoll_event epoll_event;
	struct ioevent *new_ioevent;
	int event, r;

	if (!ioeventfd_avail)
		return -ENOSYS;

	new_ioevent = malloc(sizeof(*new_ioevent));
	if (!new_ioevent)
		return -ENOMEM;

	*new_ioevent = *ioevent;
	event = new_ioevent->fd;

	kvm_ioevent = (struct kvm_ioeventfd) {
		.addr		= ioevent->io_addr,
		.len		= ioevent->io_len,
		.datamatch	= ioevent->datamatch,
		.fd		= event,
		.flags		= KVM_IOEVENTFD_FLAG_DATAMATCH,
	};

	/*
	 * For architectures that don't recognize PIO accesses, always register
	 * on the MMIO bus. Otherwise PIO accesses will cause returns to
	 * userspace.
	 */
	if (KVM_IOEVENTFD_HAS_PIO && flags & IOEVENTFD_FLAG_PIO)
		kvm_ioevent.flags |= KVM_IOEVENTFD_FLAG_PIO;

	r = ioctl(ioevent->broiler->vm_fd, KVM_IOEVENTFD, &kvm_ioevent);
	if (r) {
		r = -errno;
		goto cleanup;
	}

	if (flags & IOEVENTFD_FLAG_USER_POLL) {
		epoll_event = (struct epoll_event) {
			.events		= EPOLLIN,
			.data.ptr	= new_ioevent,
		};

		r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event, &epoll_event);
		if (r) {
			r = -errno;
			goto cleanup;
		}
	}
	new_ioevent->flags = kvm_ioevent.flags;
	list_add_tail(&new_ioevent->list, &used_ioevents);

	return 0;

cleanup:
	free(new_ioevent);
	return r;
}

int ioeventfd_del_event(u64 addr, u64 datamatch)
{
	struct kvm_ioeventfd kvm_ioevent;
	struct ioevent *ioevent;
	u8 found = 0;

	if (!ioeventfd_avail)
		return -ENOSYS;

	list_for_each_entry(ioevent, &used_ioevents, list) {
		if (ioevent->io_addr == addr &&
			ioevent->datamatch == datamatch) {
			found = 1;
			break;
		}
	}

	if (found == 0 || ioevent == NULL)
		return -ENOENT;

	kvm_ioevent = (struct kvm_ioeventfd) {
		.fd		= ioevent->fd,
		.addr		= ioevent->io_addr,
		.len		= ioevent->io_len,
		.datamatch	= ioevent->datamatch,
		.flags		= ioevent->flags |
					KVM_IOEVENTFD_FLAG_DEASSIGN,
	};

	ioctl(ioevent->broiler->vm_fd, KVM_IOEVENTFD, &kvm_ioevent);
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ioevent->fd, NULL);
	list_del(&ioevent->list);

	close(ioevent->fd);
	free(ioevent);

	return 0;
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
