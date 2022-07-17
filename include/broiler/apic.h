// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_APIC_H
#define _BROILER_APIC_H

#include "broiler/apicdef.h"

/*
 * APIC, IOAPIC stuff
 */
#define APIC_BASE_ADDR_STEP		0x00400000
#define IOAPIC_BASE_ADDR_STEP		0x00100000

#define APIC_ADDR(apic)		(APIC_DEFAULT_PHYS_BASE + apic * \
				 APIC_BASE_ADDR_STEP)
#define IOAPIC_ADDR(ioapic)	(IO_APIC_DEFAULT_PHYS_BASE + ioapic * \
				 IOAPIC_BASE_ADDR_STEP)

#define KVM_APIC_VERSION	0x14	/* xAPIC */

#endif
