#include "broiler/broiler.h"
#include "broiler/term.h"
#include "broiler/ioport.h"
#include "broiler/irq.h"
#include "broiler/utils.h"
#include <sys/prctl.h>
#include <signal.h>
#include <poll.h>
#include <pty.h>

static struct termios orig_term;
static int term_fds[TERM_MAX_DEVS][2];
static pthread_t term_poll_thread;
static int sysrq_pending;

static struct serial8250_device broiler_serial_devices[] = {
	/* ttyS0 */
	[0] = {
		.dev = {
			.bus_type	= SERIAL8250_BUS_TYPE,
			.data		= NULL,
		},
		.mutex			= MUTEX_INITIALIZER,
		.id			= 0,
		.iobase			= serial_iobase(0),
		.irq			= serial_irq(0),

		SERIAL_REGS_SETTING
	},
	/* ttyS1 */
	[1] = {
		.dev = {
			.bus_type	= SERIAL8250_BUS_TYPE,
			.data		= NULL,
		},
		.mutex			= MUTEX_INITIALIZER,
		.id			= 1,
		.iobase			= serial_iobase(1),
		.irq			= serial_irq(1),

		SERIAL_REGS_SETTING
	},
	/* ttyS2 */
	[2] = {
		.dev = {
			.bus_type	= SERIAL8250_BUS_TYPE,
			.data		= NULL,
		},
		.mutex			= MUTEX_INITIALIZER,
		.id			= 2,
		.iobase			= serial_iobase(2),
		.irq			= serial_irq(2),

		SERIAL_REGS_SETTING
	},
	/* ttyS3 */
	[3] = {
		.dev = {
			.bus_type	= SERIAL8250_BUS_TYPE,
			.data		= NULL,
		},
		.mutex			= MUTEX_INITIALIZER,
		.id			= 3,
		.iobase			= serial_iobase(3),
		.irq			= serial_irq(3),

		SERIAL_REGS_SETTING
	},
};

static int term_getc(struct broiler *broiler, int term)
{
	static bool term_got_escape = false;
	unsigned char c;
        
	if (read_in_full(term_fds[term][TERM_FD_IN], &c, 1) < 0)
		return -1;

	if (term_got_escape) {
		term_got_escape = false;
		if (c == 'x')
			broiler_reboot(broiler);
		if (c == term_escape_char)
			return c;
	}

	if (c == term_escape_char) {
		term_got_escape = true;
		return -1;
	}
        
	return c;
}

static int term_putc(char *addr, int cnt, int term)
{
	int num_remaining = cnt;
	int ret;

	while (num_remaining) {
		ret = write(term_fds[term][TERM_FD_OUT], addr, num_remaining);
		if (ret < 0)
			return cnt - num_remaining;
		num_remaining -= ret;
		addr += ret;
	}

	return cnt;
}

static void serial8250_rx(struct serial8250_device *dev, void *data)
{
	if (dev->rxdone == dev->rxcnt)
		return;

	/* Break issued ? */
	if (dev->lsr & UART_LSR_BI) {
		dev->lsr &= ~UART_LSR_BI;
		ioport_write8(data, 0);
		return;
	}

	ioport_write8(data, dev->rxbuf[dev->rxdone++]);
	if (dev->rxcnt == dev->rxdone) {
		dev->lsr &= ~UART_LSR_DR;
		dev->rxcnt = dev->rxdone = 0;
	}
}

static void serial8250_flush_tx(struct broiler *broiler,
					struct serial8250_device *dev)
{
	dev->lsr |= UART_LSR_TEMT | UART_LSR_THRE;

	if (dev->txcnt) {
		term_putc(dev->txbuf, dev->txcnt, dev->id);
		dev->txcnt = 0;
	}
}

static void serial8250_update_irq(struct broiler *broiler,
				struct serial8250_device *dev)
{
	u8 iir = 0;

	/* Handle clear rx */
	if (dev->lcr & UART_FCR_CLEAR_RCVR) {
		dev->lcr &= ~UART_FCR_CLEAR_RCVR;
		dev->rxcnt = dev->rxdone = 0;
		dev->lsr &= ~UART_LSR_DR;
	}

	/* Handle clear tx */
	if (dev->lcr & UART_FCR_CLEAR_XMIT) {
		dev->lcr &= ~UART_FCR_CLEAR_XMIT;
		dev->txcnt = 0;
		dev->lsr |= UART_LSR_TEMT | UART_LSR_THRE;
	}

	/* Data ready and rcv interrupt enabled ? */
	if ((dev->ier & UART_IER_RDI) && (dev->lsr & UART_LSR_DR))
		iir |= UART_IIR_RDI;

	/* Transmitter empty and interrupt enabled ? */
	if ((dev->ier & UART_IER_THRI) && (dev->lsr & UART_LSR_TEMT))
		iir |= UART_IIR_THRI;

	/* Now update the irq line, if necessary */
	if (!iir) {
		dev->iir = UART_IIR_NO_INT;
		if (dev->irq_state)
			broiler_irq_line(broiler, dev->irq, 0);
	} else {
		dev->iir = iir;
		if (!dev->irq_state)
		broiler_irq_line(broiler, dev->irq, 1);
	}
	dev->irq_state = iir;

	/*
	 * If the kernel disabled the tx interrupt, we know that there
	 * is nothing more to transmit, so we can reset our tx logic
	 * here.
	 */
	if (!(dev->ier & UART_IER_THRI))
		serial8250_flush_tx(broiler, dev);
}

static void
serial8250_sysrq(struct broiler *broiler, struct serial8250_device *dev)
{
	dev->lsr |= UART_LSR_DR | UART_LSR_BI;
	dev->rxbuf[dev->rxcnt++] = sysrq_pending;
	sysrq_pending = SYSRQ_PENDING_NONE;
}

bool term_readable(int term)
{
	struct pollfd pollfd = (struct pollfd) {
		.fd	= term_fds[term][TERM_FD_IN],
		.events	= POLLIN,
		.revents = 0,
	};      
	int err;

	err = poll(&pollfd, 1, 0);
	return (err > 0 && (pollfd.revents & POLLIN));
}

static void serial8250_receive(struct broiler *broiler,
			struct serial8250_device *dev, bool handle_sysrq)
{
	int c;

	if (dev->mcr & UART_MCR_LOOP)
		return;

	if ((dev->lsr & UART_LSR_DR) || dev->rxcnt)
		return;

	if (handle_sysrq && sysrq_pending) {
		serial8250_sysrq(broiler, dev); 
		return;
	}

	while (term_readable(dev->id) &&
		dev->rxcnt < FIFO_LEN) {

		c = term_getc(broiler, dev->id);

		if (c < 0)
			break;
		dev->rxbuf[dev->rxcnt++] = c;
		dev->lsr |= UART_LSR_DR;
	}
}

static void serial8250_update_consoles(struct broiler *broiler)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(broiler_serial_devices); i++) {
		struct serial8250_device *dev = &broiler_serial_devices[i];

		mutex_lock(&dev->mutex);

		/* Restrict sysrq inject to the first port */
		serial8250_receive(broiler, dev, i == 0);

		serial8250_update_irq(broiler, dev);

		mutex_unlock(&dev->mutex);
	}
}

static void *term_poll_thread_loop(void *param)
{
	struct broiler *broiler = (struct broiler *)param;
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
		serial8250_update_consoles(broiler);
		/* wait virtio-serial inject interrupt */
		;
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

static bool serial8250_out(struct serial8250_device *dev,
			struct broiler_cpu *vcpu, u16 offset, void *data)
{
	bool ret = true;
	char *addr = data;

	mutex_lock(&dev->mutex);

	switch (offset) {
	case UART_TX:
		if (dev->lcr & UART_LCR_DLAB) {
			dev->dll = ioport_read8(data);
			break;
		}

		/* Loopback mode */
		if (dev->mcr & UART_MCR_LOOP) {
			if (dev->rxcnt < FIFO_LEN) {
				dev->rxbuf[dev->rxcnt++] = *addr;
				dev->lsr |= UART_LSR_DR;
			}
			break;
		}

		if (dev->txcnt < FIFO_LEN) {
			dev->txbuf[dev->txcnt++] = *addr;
			dev->lsr &= ~UART_LSR_TEMT;
			if (dev->txcnt == FIFO_LEN / 2)
				dev->lsr &= ~UART_LSR_THRE;
			serial8250_flush_tx(vcpu->broiler, dev);
		} else {
			/* Should never happpen */
			dev->lsr &= ~(UART_LSR_TEMT | UART_LSR_THRE);
		}
		break;
	case UART_IER:
		if (!(dev->lcr & UART_LCR_DLAB))
			dev->ier = ioport_read8(data) & 0x0f;
		else
			dev->dlm = ioport_read8(data);
		break;
	case UART_FCR:
		dev->fcr = ioport_read8(data);
		break;
	case UART_LCR:
		dev->lcr = ioport_read8(data);
		break;
	case UART_MCR:
		dev->mcr = ioport_read8(data);
		break;
	case UART_LSR:
		/* Factory test */
		break;
	case UART_MSR:
		/* Not used */
		break;
	case UART_SCR:
		dev->scr = ioport_read8(data);
		break;
	default:
		ret = false;
		break;
	}

	serial8250_update_irq(vcpu->broiler, dev);
	mutex_unlock(&dev->mutex);

	return ret;
}

static bool serial8250_in(struct serial8250_device *dev,
		struct broiler_cpu *vcpu, u16 offset, void *data)
{
	bool ret = true;

	mutex_lock(&dev->mutex);

	switch (offset) {
	case UART_RX:
		if (dev->lcr & UART_LCR_DLAB)
			ioport_write8(data, dev->dll);
		else
			serial8250_rx(dev, data);
		break;
	case UART_IER:
		if (dev->lcr & UART_LCR_DLAB)
			ioport_write8(data, dev->dlm);
		else
			ioport_write8(data, dev->ier);
	break;
	case UART_IIR:
		ioport_write8(data, dev->iir | UART_IIR_TYPE_BITS);
		break;
	case UART_LCR:
		ioport_write8(data, dev->lcr);
		break;
	case UART_MCR:
		ioport_write8(data, dev->mcr);
		break;
	case UART_LSR:
		ioport_write8(data, dev->lsr);
		break;
	case UART_MSR:
		ioport_write8(data, dev->msr);
		break;
	case UART_SCR:
		ioport_write8(data, dev->scr);
		break;
	default:
		ret = false;
		break;
	}

	serial8250_update_irq(vcpu->broiler, dev);
	mutex_unlock(&dev->mutex);

	return ret;
}

static void serial8250_mmio(struct broiler_cpu *vcpu, u64 addr, u8 *data,
		u32 len, u8 is_write, void *ptr)
{
	struct serial8250_device *dev = ptr;

	if (is_write)
		serial8250_out(dev, vcpu, addr - dev->iobase, data);
	else
		serial8250_in(dev, vcpu, addr - dev->iobase, data);
}

static int serial8250_device_init(struct broiler *broiler,
					struct serial8250_device *dev)
{
	int r;

	r = device_register(&dev->dev);
	if (r < 0)
		return r;

	r = broiler_ioport_register(broiler, dev->iobase, 8,
			serial8250_mmio, dev, SERIAL8250_BUS_TYPE);

	return r;
}

static int serial8250_init(struct broiler *broiler)
{
	unsigned int i, j;
	int r = 0;

	for (i = 0; i < ARRAY_SIZE(broiler_serial_devices); i++) {
		struct serial8250_device *dev = &broiler_serial_devices[i];

		r = serial8250_device_init(broiler, dev);
		if (r < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	for (j = 0; i <= i; j++) {
		struct serial8250_device *dev = &broiler_serial_devices[j];

		broiler_ioport_deregister(broiler, dev->iobase,
						SERIAL8250_BUS_TYPE);
		device_unregister(&dev->dev);
	}
	return r;
}

static int serial8250_exit(struct broiler *broiler)
{
	unsigned int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(broiler_serial_devices); i++) {
		struct serial8250_device *dev = &broiler_serial_devices[i];

		r = broiler_ioport_deregister(broiler, dev->iobase,
				SERIAL8250_BUS_TYPE);
		if (r < 0)
			return r;
		device_unregister(&dev->dev);
	}
	return 0;
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

	/* Intel 8250 */
	serial8250_init(broiler);

	return 0;
}

int broiler_terminal_exit(struct broiler *broiler)
{
	serial8250_exit(broiler);
	return 0;
}
