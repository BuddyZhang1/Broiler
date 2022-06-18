#include "broiler/broiler.h"
#include "broiler/term.h"
#include <sys/prctl.h>
#include <signal.h>
#include <poll.h>
#include <pty.h>

static struct termios orig_term;
static int term_fds[TERM_MAX_DEVS][2];
static pthread_t term_poll_thread;

static void *term_poll_thread_loop(void *param)
{
	struct broiler *broler = (struct broiler *)param;
	struct pollfd fds[TERM_MAX_DEVS];
	int i;

	prctl(PR_SET_NAME, "term-poll");

	for (i = 0; i < TERM_MAX_DEVS; i++) {
		fds[i].fd = term_fds[i][TERM_FD_IN];
		fds[i].events = POLLIN;
		fds[i].revents = 0;
	}

	while (1) {
		/* Poll with infinite timeout */
		if (poll(fds, TERM_MAX_DEVS, -1) < 1)
			break;
		//broiler_read_term(broiler);
	}
	printf("term_poll_thread_loop: error polling device fds %d\n", errno);
	return NULL;
}

static void term_cleanup(void)
{
	int i;

	for (i = 0; i < TERM_MAX_DEVS; i++)
		tcsetattr(term_fds[i][TERM_FD_IN], TCSANOW, &orig_term);
}

static void term_sig_cleanup(int sig)
{
	term_cleanup();
	signal(sig, SIG_DFL);
	raise(sig);
}

int broiler_terminal_init(struct broiler *broiler)
{
	struct termios term;
	int i, r;

	for (i = 0; i < TERM_MAX_DEVS; i++)
		if (term_fds[i][TERM_FD_IN] == 0) {
			term_fds[i][TERM_FD_IN] = STDIN_FILENO;
			term_fds[i][TERM_FD_OUT] = STDOUT_FILENO;
		}

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
		return 0;

	r = tcgetattr(STDIN_FILENO, &orig_term);
	if (r < 0) {
		printf("Unable to save initial standard input settings.\n");
		return r;
	}

	term = orig_term;
	term.c_iflag &= ~(ICRNL);
	term.c_lflag &= ~(ICANON | ECHO | ISIG);
	tcsetattr(STDIN_FILENO, TCSANOW, &term);

	/* Use our own blocking thread to read stdin, don't require a tick */
	if (pthread_create(&term_poll_thread, NULL,
					term_poll_thread_loop, broiler)) {
		printf("Unable to create console input poll thread\n");
		exit(1);
	}

	signal(SIGTERM, term_sig_cleanup);
	atexit(term_cleanup);

	return 0;
}

int broiler_terminal_exit(struct broiler *broiler)
{
	return 0;
}
