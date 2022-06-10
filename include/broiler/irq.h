#ifndef _BISCUITOS_IRQ_H
#define _BISCUITOS_IRQ_H

/* Those definitions are generic FDT values for specifying IRQ
 * types and are used in the Linux kernel internally as well as in
 * the dts files and their documentation.
 */
enum irq_type {
	IRQ_TYPE_NONE         = 0x00000000,
	IRQ_TYPE_EDGE_RISING  = 0x00000001,
	IRQ_TYPE_EDGE_FALLING = 0x00000002,
	IRQ_TYPE_EDGE_BOTH    = (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING),
	IRQ_TYPE_LEVEL_HIGH   = 0x00000004,
	IRQ_TYPE_LEVEL_LOW    = 0x00000008,
	IRQ_TYPE_LEVEL_MASK   = (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH),
};

#endif
