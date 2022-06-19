#ifndef _BISCUITOS_TERM_H
#define _BISCUITOS_TERM_H

#include "broiler/broiler.h"
#include "broiler/device.h"
#include "linux/mutex.h"
#include <linux/serial_reg.h>

#define TERM_FD_IN	0
#define TERM_FD_OUT	1

#define TERM_MAX_DEVS	4
#define FIFO_LEN	64
#define FIFO_MASK	(FIFO_LEN - 1)

/* 8250 SERIAL */
#define serial_iobase_0		(BROILER_IOPORT_AREA + 0x3f8)
#define serial_iobase_1		(BROILER_IOPORT_AREA + 0x2f8)
#define serial_iobase_2		(BROILER_IOPORT_AREA + 0x3e8)
#define serial_iobase_3		(BROILER_IOPORT_AREA + 0x2e8)
#define serial_irq_0		4
#define serial_irq_1		3
#define serial_irq_2		4
#define serial_irq_3		3
#define serial_iobase(nr)	serial_iobase_##nr
#define serial_irq(nr)		serial_irq_##nr
#define SERIAL8250_BUS_TYPE	DEVICE_BUS_IOPORT
/* ctrl-a is used for escape */
#define term_escape_char	0x01

#define UART_IIR_TYPE_BITS	0xc0
#define SYSRQ_PENDING_NONE	0

struct serial8250_device {
	struct device	dev;
	struct mutex	mutex;
	u8	id;

	u32	iobase;
	u8	irq;
	u8	irq_state;
	int	txcnt;
	int	rxcnt;
	int	rxdone;
	char	txbuf[FIFO_LEN];
	char	rxbuf[FIFO_LEN];

	u8	dll;
	u8	dlm;
	u8	iir;
	u8	ier;
	u8	fcr;
	u8	lcr;
	u8	mcr;
	u8	lsr;
	u8	msr;
	u8	scr;
};

#define SERIAL_REGS_SETTING \
	.iir                    = UART_IIR_NO_INT, \
	.lsr                    = UART_LSR_TEMT | UART_LSR_THRE, \
	.msr                    = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS, \
	.mcr                    = UART_MCR_OUT2,

#endif
