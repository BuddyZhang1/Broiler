#ifndef _BISCUITOS_IOPORT_H
#define _BISCUITOS_IOPORT_H

#include "broiler/kvm.h"
#include "broiler/device.h"
#include "linux/rbtree-interval.h"

/*      
 * We are reusing the existing DEVICE_BUS_MMIO and DEVICE_BUS_IOPORT constants
 * from kvm/devices.h to differentiate between registering an I/O port and an
 * MMIO region. 
 * To avoid collisions with future additions of more bus types, we reserve
 * a generous 4 bits for the bus mask here.
 */             
#define IOPORT_BUS_MASK		0xf
#define IOPORT_COALESCE		(1U << 4)

typedef void (*mmio_handler_fn)(struct kvm_cpu *vcpu, u64 addr, 
				u8 *data, u32 len, u8 is_write, void *ptr);

struct mmio_mapping {
	struct rb_int_node	node;
	mmio_handler_fn		mmio_fn;
	void			*ptr;
	u32			refcount;
	bool			remove;
};

extern int broiler_ioport_register(struct broiler *broiler, u64 phys_addr,
                        u64 phys_addr_len, mmio_handler_fn mmio_fn,
                        void *ptr, unsigned int flags);

extern bool broiler_ioport_deregister(struct broiler *broiler,
                                u64 phys_addr, unsigned int flags);

static inline int
broiler_register_pio(struct broiler *broiler, u16 port, u16 len,
					mmio_handler_fn mmio_fn, void *ptr)
{
	return broiler_ioport_register(broiler, port,
				len, mmio_fn, ptr, DEVICE_BUS_IOPORT);
}

static inline int
broiler_register_mmio(struct broiler *broiler, u64 phys_addr,
	u64 len, bool coalesce, mmio_handler_fn mmio_fn, void *ptr)
{
	return broiler_ioport_register(broiler, phys_addr, len,
				mmio_fn, ptr, DEVICE_BUS_MMIO |
				(coalesce ? IOPORT_COALESCE : 0));
}

static inline bool
broiler_deregister_pio(struct broiler *broiler, u16 port)
{
	return broiler_ioport_deregister(broiler, port, DEVICE_BUS_IOPORT);
}

static inline u8 ioport_read8(u8 *data)
{
	return *data;
}

static inline void ioport_write8(u8 *data, u8 value)
{
	*data = value;
}

static inline u16 ioport_read16(u16 *data)
{
	return *data;
}

static inline void ioport_write16(u16 *data, u16 value)
{
	*data = value;
}

static inline u32 ioport_read32(u32 *data)
{
	return *data;
}

static inline void ioport_write32(u32 *data, u32 value)
{
	*data = value;
}

#endif
