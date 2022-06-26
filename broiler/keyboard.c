#include "broiler/broiler.h"
#include "broiler/keyboard.h"
#include "broiler/ioport.h"
#include "broiler/irq.h"
#include "broiler/kvm.h"

static struct kbd_state keyboard_state;

static void kbd_reset(void)
{
	keyboard_state = (struct kbd_state) {
		.status	= I8042_STR_MUXERR | 
				I8042_STR_CMDDAT | I8042_STR_KEYLOCK, /* 0x1c */
		.mode	= KBD_MODE_KBD_INT | KBD_MODE_SYS, /* 0x3 */
		.mres	= AUX_DEFAULT_RESOLUTION,
		.msample	= AUX_DEFAULT_SAMPLE,
	};
}

/*
 * If there are packets to be read, set the appropriate IRQs high
 */
static void kbd_update_irq(void)
{
	u8 klevel = 0;
	u8 mlevel = 0;

	/* First, clear the kbd and aux output buffer faull bits */
	keyboard_state.status &= ~(I8042_STR_OBF | I8042_STR_AUXDATA);

	if (keyboard_state.kcount > 0) {
		keyboard_state.status |= I8042_STR_OBF;
		klevel = 1;
	}

	/* Keyboard has higher priority than mouse */
	if (klevel == 0 && keyboard_state.mcount != 0) {
		keyboard_state.status |= I8042_STR_OBF | I8042_STR_AUXDATA;
		mlevel = 1;
	}

	broiler_irq_line(keyboard_state.broiler, KBD_IRQ, klevel);
	broiler_irq_line(keyboard_state.broiler, AUX_IRQ, mlevel);
}

/* Add a byte to the keyboard queue, then set IRQs */
static void kbd_queue(u8 c)
{
	if (keyboard_state.kcount >= QUEUE_SIZE)
		return;

	keyboard_state.kq[keyboard_state.kwrite++ % QUEUE_SIZE] = c;

	keyboard_state.kcount++;
	kbd_update_irq();
}

/* Add a byte to the mouse queue, then set IRQs */
static void mouse_queue(u8 c)
{
	if (keyboard_state.mcount >= QUEUE_SIZE)
		return;

	keyboard_state.mq[keyboard_state.mwrite++ % QUEUE_SIZE] = c;

	keyboard_state.mcount++;
	kbd_update_irq();
}

static void kbd_write_command(struct broiler *broiler, u8 val)
{
	switch (val) {
	case I8042_CMD_CTL_RCTR:
		kbd_queue(keyboard_state.mode);
		break;
	case I8042_CMD_CTL_WCTR:
	case I8042_CMD_AUX_SEND:
	case I8042_CMD_AUX_LOOP:
		keyboard_state.write_cmd = val;
		break;
	case I8042_CMD_AUX_TEST:
		/* 0 means we're a normal PS/2 mouse */
		mouse_queue(0);
		break;
	case I8042_CMD_AUX_DISABLE:
		keyboard_state.mode |= MODE_DISABLE_AUX;
		break;
	case I8042_CMD_AUX_ENABLE:
		keyboard_state.mode &= ~MODE_DISABLE_AUX;
		break;
	case I8042_CMD_SYSTEM_RESET:
		broiler_reboot(broiler);
		break;
	default:
		break;
	}
}

/* Callled when the OS read from port 0x64, the command port */
static u8 kbd_read_status(void)
{
	return keyboard_state.status;
}

/*
 * Called when the OS writes to port 0x60 (data port)
 * Things written here are generally arguments to commands previously
 * written to port 0x64 and stored in state.write_cmd
 */
static void kbd_write_data(u8 val)
{
	switch (keyboard_state.write_cmd) {
	case I8042_CMD_CTL_WCTR:
		keyboard_state.mode = val;
		kbd_update_irq();
		break;
	case I8042_CMD_AUX_LOOP:
		mouse_queue(val);
		mouse_queue(RESPONSE_ACK);
		break;
	case I8042_CMD_AUX_SEND:
		/* The OS wants to send a command to the mouse */
		mouse_queue(RESPONSE_ACK);
		switch (val) {
		case 0xe6:
			/* set scaling = 1:1 */
			keyboard_state.mstatus &= ~AUX_SCALING_FLAG;
			break;
		case 0xe8:
			/* set resolution */
			keyboard_state.mres = val;
			break;
		case 0xe9:
			/* Report mouse status/config */
			mouse_queue(keyboard_state.mstatus);
			mouse_queue(keyboard_state.mres);
			mouse_queue(keyboard_state.msample);
			break;
		case 0xf2:
			/* send ID */
			mouse_queue(0); /* normal mouse */
			break;
		case 0xf3:
			/* set sample rate */
			keyboard_state.msample = val;
			break;
		case 0xf4:
			/* enable reporting */
			keyboard_state.mstatus |= AUX_ENABLE_REPORTING;
			break;
		case 0xf5:
			keyboard_state.mstatus &= ~AUX_ENABLE_REPORTING;
			break;
		case 0xf6:
			/* set defaults, just fall through to reset */
		case 0xff:
			/* reset */
			keyboard_state.mstatus = 0x0;
			keyboard_state.mres = AUX_DEFAULT_RESOLUTION;
			keyboard_state.msample = AUX_DEFAULT_SAMPLE;
			break;
		default:
			break;
		}
		break;
	case 0:
		/* Just send the ID */
		kbd_queue(RESPONSE_ACK);
		kbd_queue(0xab);
		kbd_queue(0x41);
		kbd_update_irq();
		break;
	default:
		/* Yeah whatever */
		break;
	}
	keyboard_state.write_cmd = 0;
}

/* Called when the OS reads from port 0x60 (PS/2 data) */
static u8 kbd_read_data(void)
{
	u8 ret;
	int i;

	if (keyboard_state.kcount != 0) {
		/* Keyboard data gets read first */
		ret = keyboard_state.kq[keyboard_state.kread++ % QUEUE_SIZE];
		keyboard_state.kcount--;
		broiler_irq_line(keyboard_state.broiler, KBD_IRQ, 0);
		kbd_update_irq();
	} else if (keyboard_state.mcount > 0) {
		/* Followed by the mouse */
		ret = keyboard_state.mq[keyboard_state.mread++ % QUEUE_SIZE];
		keyboard_state.mcount--;
		broiler_irq_line(keyboard_state.broiler, AUX_IRQ, 0);
		kbd_update_irq();
	} else {
		i = keyboard_state.kread - 1;
		if (i < 0)
			i = QUEUE_SIZE;
		ret = keyboard_state.kq[i];
	}
	return ret;
}

static void kbd_io(struct broiler_cpu *vcpu, u64 addr, u8 *data, u32 len,
				u8 is_write, void *ptr)
{
	u8 value;

	if (is_write)
		value = ioport_read8(data);

	switch (addr) {
	case I8042_COMMAND_REG:
		if (is_write)
			kbd_write_command(vcpu->broiler, value);
		else
			value = kbd_read_status();
		break;
	case I8042_DATA_REG:
		if (is_write)
			kbd_write_data(value);
		else
			value = kbd_read_data();
		break;
	case I8042_PORT_B_REG:
		if (!is_write)
			value = 0x20;
		break;
	default:
		return;
	}
	if (!is_write)
		ioport_write8(data, value);
}

int broiler_keyboard_init(struct broiler *broiler)
{
	int r;

	kbd_reset();
	keyboard_state.broiler = broiler;
	r = broiler_register_pio(broiler, I8042_DATA_REG, 2, kbd_io, NULL);
	if (r < 0)
		return r;
	r = broiler_register_pio(broiler, I8042_COMMAND_REG, 2, kbd_io, NULL);
	if (r < 0) {
		broiler_deregister_pio(broiler, I8042_DATA_REG);
		return r;
	}

	return 0;
}
