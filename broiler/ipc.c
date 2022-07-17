// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/ipc.h"
#include "broiler/utils.h"
#include "broiler/kvm.h"
#include "linux/rwsem.h"
#include <linux/limits.h>
#include <linux/un.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/eventfd.h>
#include <malloc.h>

static int epoll_fd, server_fd, stop_fd;
static pthread_t broiler_ipc_thread;
static DECLARE_RWSEM(msgs_rwlock);
static void (*broiler_msgs[BROILER_IPC_MAX_MSGS])(struct broiler *broiler, 
				int fd, u32 type, u32 len, u8 *msg);

static void broiler_remove_socket(void)
{
	char full_name[PATH_MAX];

	snprintf(full_name, sizeof(full_name), "%s/%s%s",
			".", ".Broiler", BROILER_SOCK_SUFFIX);
	unlink(full_name);
}

static int broiler_create_socket(struct broiler *broiler)
{
	char full_name[PATH_MAX];
	struct sockaddr_un local;
	int s, len, r;

	snprintf(full_name, sizeof(full_name), "%s/%s%s",
			".", ".Broiler", BROILER_SOCK_SUFFIX);
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		perror("socket");
		return s;
	}

	local.sun_family = AF_UNIX;
	strlcpy(local.sun_path, full_name, sizeof(local.sun_path));
	len = strlen(local.sun_path) + sizeof(local.sun_family);
	r = bind(s, (struct sockaddr *)&local, len);
	/* Check for an existing socket file */
	if (r < 0 && errno == EADDRINUSE) {
		r = connect(s, (struct sockaddr *)&local, len);
		if (r == 0) {
			/*
			 * If we could connect, there is already a guest
			 * using this same name. This should not happen
			 * for PID derived names, but could happen for
			 * user provided guest names.
			 */
			printf("Guest socket file %s already exists.\n",
						full_name);
			r = -EEXIST;
			goto fail;
		}
		if (errno == ECONNREFUSED) {
			/*
			 * This is a ghost socket file, with no-one
			 * listening on the other end. Since Broiler will
			 * only bind above when creating a new guest,
			 * there is no danger in just removing the file
			 * and re-trying.
			 */
			unlink(full_name);
			printf("Removed ghost socket file %s,\n", full_name);
			r = bind(s, (struct sockaddr *)&local, len);
		}
	}
	if (r < 0) {
		perror("bind");
		goto fail;
	}

	r = listen(s, 5);
	if (r < 0) {
		perror("listen");
		goto fail;
	}

	return s;

fail:
	close(s);
	return r;

}

static int broiler_ipc_new_conn(int fd)
{
	struct epoll_event ev;
	int client;

	client = accept(fd, NULL, NULL);
	if (client < 0)
		return -1;

	ev.events = EPOLLIN | EPOLLRDHUP;
	ev.data.fd = client;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client, &ev) < 0) {
		close(client);
		return -1;
	}
	return client;
}

static int broiler_ipc_handle(struct broiler *broiler,
				int fd, u32 type, u32 len, u8 *data)
{
	void (*cb)(struct broiler *broiler, int fd, u32 type,
						u32 len, u8 *msg);

	if (type >= BROILER_IPC_MAX_MSGS)
		return -ENOSPC;

	down_read(&msgs_rwlock);
	cb = broiler_msgs[type];
	up_read(&msgs_rwlock);

	if (cb == NULL) {
		printf("No device handles type %u\n", type);
		return -ENODEV;
	}

	cb(broiler, fd, type, len, data);
	return 0;
}

static int broiler_ipc_receive(struct broiler *broiler, int fd)
{
	struct broiler_ipc_head head;
	u8 *msg = NULL;
	u32 n;

	n = read(fd, &head, sizeof(head));
	if (n != sizeof(head))
		goto done;

	msg = malloc(head.len);
	if (msg == NULL)
		goto done;

	n = read_in_full(fd, msg, head.len);
	if (n != head.len)
		goto done;

	broiler_ipc_handle(broiler, fd, head.type, head.len, msg);

	return 0;

done:
	free(msg);
	return -1;
}

static void broiler_ipc_close_conn(int fd)
{
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
	close(fd);
}

static void *broiler_ipc_thread_fn(void *param)
{
	struct epoll_event event;
	struct broiler *broiler = param;

	prctl(PR_SET_NAME, "Broiler-ipc");

	for (;;) {
		int nfds;

		nfds = epoll_wait(epoll_fd, &event, 1, -1);
		if (nfds > 0) {
			int fd = event.data.fd;

			if (fd == stop_fd && event.events & EPOLLIN) {
				break;
			} else if (fd == server_fd) {
				int client, r;

				client = broiler_ipc_new_conn(fd);
				/* Handle multiple IPC cmd at a time */
				do {
					r = broiler_ipc_receive(broiler,
								   client);
				} while (r == 0);
			} else if (event.events & (EPOLLERR | EPOLLRDHUP |
							EPOLLHUP)) {
				broiler_ipc_close_conn(fd);
			} else
				broiler_ipc_receive(broiler, fd);
		}
	}

	return NULL;
}

static int broiler_ipc_register_handler(u32 type, void (*cb)(
	struct broiler *broiler, int fd, u32 type, u32 len, u8 *msg))
{
	if (type >= BROILER_IPC_MAX_MSGS)
		return -ENOSPC;

	down_write(&msgs_rwlock);
	broiler_msgs[type] = cb;
	up_write(&msgs_rwlock);

	return 0;
}

static void
broiler_pid(struct broiler *broiler, int fd, u32 type, u32 len, u8 *msg)
{
	pid_t pid = getpid();
	int r = 0;

	if (type == BROILER_IPC_PID)
		r = write(fd, &pid, sizeof(pid));

	if (r < 0)
		perror("Failed sending PID");
}

static void handle_sigusr1(int sig)
{
	struct broiler_cpu *cpu = current_broiler_cpu;

	printf("TODO....\n");
}

int broiler_ipc_init(struct broiler *broiler)
{
	int sock = broiler_create_socket(broiler);
	struct epoll_event ev = {0};
	int ret;

	server_fd = sock;
	epoll_fd = epoll_create(BROILER_IPC_MAX_MSGS);
	if (epoll_fd < 0) {
		perror("epoll_create");
		ret = epoll_fd;
		goto err;
	}

	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = sock;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev) < 0) {
		perror("Failed adding socket to epoll");
		ret = -EFAULT;
		goto err_epoll;
	}

	stop_fd = eventfd(0, 0);
	if (stop_fd < 0) {
		perror("eventfd");
		ret = stop_fd;
		goto err_epoll;
	}

	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = stop_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, stop_fd, &ev) < 0) {
		perror("Failed adding stop event to epoll");
		ret = -EFAULT;
		goto err_stop;
	}

	if (pthread_create(&broiler_ipc_thread, NULL, 
				broiler_ipc_thread_fn, broiler) != 0) {
		perror("Failed starting IPC thread");
		ret = -EFAULT;
		goto err_stop;
	}

	broiler_ipc_register_handler(BROILER_IPC_PID, broiler_pid);

	signal(SIGUSR1, handle_sigusr1);

	return 0;

err_stop:
	close(stop_fd);
err_epoll:
	close(epoll_fd);
err:
	return ret;
}

int broiler_ipc_exit(struct broiler *broiler)
{
	u64 val = 1;
	int ret;

	ret = write(stop_fd, &val, sizeof(val));
	if (ret < 0)
		return ret;

	close(server_fd);
	close(epoll_fd);

	broiler_remove_socket();

	return ret;
}
