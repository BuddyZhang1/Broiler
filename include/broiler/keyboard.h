// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_KEYBOARD_H
#define _BROILER_KEYBOARD_H

#include "broiler/broiler.h"

/*
 * IRQs
 */
#define KBD_IRQ			1
#define AUX_IRQ			12

/*
 * Registers
 */
#define I8042_DATA_REG		0x60
#define I8042_PORT_B_REG	0x61
#define I8042_COMMAND_REG	0x64

/*
 * Commands
 */
#define I8042_CMD_CTL_RCTR	0x20
#define I8042_CMD_CTL_WCTR	0x60
#define I8042_CMD_AUX_LOOP	0xD3
#define I8042_CMD_AUX_SEND	0xD4
#define I8042_CMD_AUX_TEST	0xA9
#define I8042_CMD_AUX_DISABLE	0xA7
#define I8042_CMD_AUX_ENABLE	0xA8
#define I8042_CMD_SYSTEM_RESET	0xFE

#define RESPONSE_ACK		0xFA

#define MODE_DISABLE_AUX	0x20

#define AUX_ENABLE_REPORTING	0x20
#define AUX_SCALING_FLAG	0x10
#define AUX_DEFAULT_RESOLUTION	0x2
#define AUX_DEFAULT_SAMPLE	100

/*
 * Status register bits
 */
#define I8042_STR_AUXDATA	0x20
#define I8042_STR_KEYLOCK	0x10
#define I8042_STR_CMDDAT	0x08
#define I8042_STR_MUXERR	0x04
#define I8042_STR_OBF		0x01

#define KBD_MODE_KBD_INT	0x01
#define KBD_MODE_SYS		0x02

#define QUEUE_SIZE		128

/*
 * This represents the current state of the PS/2 keyboard system,
 * including the AUX device (the mouse)
 */
struct kbd_state {
	struct broiler *broiler;
	u8	kq[QUEUE_SIZE]; /* Keyboard queue */
	int	kread, kwrite;  /* Indexes into the queue */
	int	kcount;         /* number of elements in queue */

	u8	mq[QUEUE_SIZE];
	int	mread, mwrite;
	int	mcount;

	u8	mstatus;        /* Mouse status byte */
	u8	mres;           /* Current mouse resolution */
	u8	msample;        /* Current mouse samples/second */

	u8	mode;           /* i8042 mode register */
	u8	status;         /* i8042 status register */
	/*
	 * Some commands (on port 0x64) have arguments;
	 * we store the command here while we wait for the argument
	 */
	u8	write_cmd;
};


#endif
