// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_IRQ_H
#define _BROILER_IRQ_H

#include <linux/kvm.h>
#include "broiler/broiler.h"
#include "broiler/msi.h"

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

struct msi_routing_ops {
	int (*update_route)(struct broiler *broiler, struct kvm_irq_routing_entry *);	
	bool (*can_signal_msi)(struct broiler *broiler);
	int (*signal_msi)(struct broiler *broiler, struct kvm_msi *msi);
	int (*translate_gsi)(struct broiler *broiler, u32 gsi);
};

extern void broiler_irq_line(struct broiler *broiler, int irq, int level);
extern void broiler_irq_trigger(struct broiler *broiler, int irq);
extern int irq_signal_msi(struct broiler *broiler, struct kvm_msi *msi);
extern int irq_add_msix_route(struct broiler *broiler,
				struct msi_msg *msg, u32 device_id);
extern void irq_update_msix_route(struct broiler *, u32, struct msi_msg *);
extern bool irq_can_signal_msi(struct broiler *broiler);

#endif
