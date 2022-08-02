// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/kvm.h"
#include "broiler/ioport.h"
#include "broiler/pci.h"
#include "linux/mutex.h"
#include "linux/rbtree.h"

#define mmio_node(n)	rb_entry(n, struct mmio_mapping, node)
static DEFINE_MUTEX(mmio_lock);

static struct rb_root mmio_tree = RB_ROOT;
static struct rb_root pio_tree = RB_ROOT;

static void dummy_io(struct broiler_cpu *vcpu, u64 addr, 
			u8 *data, u32 len, u8 is_write, void *ptr) { }

static int mmio_insert(struct rb_root *root, struct mmio_mapping *data)
{
	rb_int_insert(root, &data->node);
}

static void mmio_remove(struct rb_root *root, struct mmio_mapping *data)
{
	rb_int_erase(root, &data->node);
}

/* Called with mmio_lock held. */
static void mmio_deregister(struct broiler *broiler,
		struct rb_root *root, struct mmio_mapping *mmio)
{
	struct kvm_coalesced_mmio_zone zone = {
		.addr = rb_int_start(&mmio->node),
		.size = 1,
	};
	ioctl(broiler->vm_fd, KVM_UNREGISTER_COALESCED_MMIO, &zone);

	mmio_remove(root, mmio);
	free(mmio);
}

/* Find lowest match, Check for overlay */
static struct mmio_mapping *mmio_search_single(struct rb_root *root, u64 addr)
{
	struct rb_int_node *node;

	node = rb_int_search_single(root, addr);
	if (node == NULL)
		return NULL;
	return mmio_node(node);
}

static bool ioport_is_mmio(unsigned int flags)
{
	return (flags & IOPORT_BUS_MASK) == DEVICE_BUS_MMIO;
}

/* The 'fast A20 gate' */

static void ps2_control_io(struct broiler_cpu *vcpu, u64 addr, u8 *data,
				u32 len, u8 is_write, void *ptr)
{
	/* A20 is always enabled */
	if (!is_write)
		ioport_write8(data, 0x02);
}

static void debug_io(struct broiler_cpu *vcpu, u64 addr, u8 *data,
				u32 len, u8 is_write, void *ptr) { }

int broiler_ioport_register(struct broiler *broiler, u64 phys_addr,
			u64 phys_addr_len, mmio_handler_fn mmio_fn,
			void *ptr, unsigned int flags)
{
	struct mmio_mapping *mmio;
	struct kvm_coalesced_mmio_zone zone;
	int ret;

	mmio = malloc(sizeof(*mmio));
	if (mmio == NULL)
		return -ENOMEM;

	*mmio = (struct mmio_mapping) {
		.node = RB_INT_INIT(phys_addr, phys_addr + phys_addr_len),
		.mmio_fn = mmio_fn,
		.ptr = ptr,
		.refcount = 0,
		.remove = false,
	};

	if (ioport_is_mmio(flags) && (flags & IOPORT_COALESCE)) {
		zone = (struct kvm_coalesced_mmio_zone) {
			.addr = phys_addr,
			.size = phys_addr_len,
		};
		if (ioctl(broiler->vm_fd,
				KVM_REGISTER_COALESCED_MMIO, &zone) < 0) {
			free(mmio);
			return -errno;
		}
	}

	mutex_lock(&mmio_lock);
	if (ioport_is_mmio(flags))
		ret = mmio_insert(&mmio_tree, mmio);
	else
		ret = mmio_insert(&pio_tree, mmio);
	mutex_unlock(&mmio_lock);

	return 0;

}

static struct mmio_mapping *
mmio_search(struct rb_root *root, u64 addr, u64 len)
{
	struct rb_int_node *node;

	/* If len is zero or if there's an overflow, the MMIO op is invalid */
	if (addr + len <= addr)
		return NULL;

	node = rb_int_search_range(root, addr, addr + len);
	if (node == NULL)
		return NULL;

	return mmio_node(node);
}

static struct mmio_mapping *mmio_get(struct rb_root *root, 
					u64 phys_addr, u32 len)
{
	struct mmio_mapping *mmio;

	mutex_lock(&mmio_lock);
	mmio = mmio_search(root, phys_addr, len);
	if (mmio)
		mmio->refcount++;
	mutex_unlock(&mmio_lock);

	return mmio;
}

static void mmio_put(struct broiler *broiler, struct rb_root *root,
				struct mmio_mapping *mmio)
{
	mutex_lock(&mmio_lock);
	mmio->refcount--;
	if (mmio->remove && mmio->refcount == 0)
		mmio_deregister(broiler, root, mmio);
	mutex_unlock(&mmio_lock);
}

bool broiler_cpu_emulate_mmio(struct broiler_cpu *vcpu, u64 phys_addr,
					u8 *data, u32 len, u8 is_write)
{
	struct mmio_mapping *mmio;

	mmio = mmio_get(&mmio_tree, phys_addr, len);
	if (!mmio)
		goto out;

	mmio->mmio_fn(vcpu, phys_addr, data, len, is_write, mmio->ptr);
	mmio_put(vcpu->broiler, &mmio_tree, mmio);
out:
	return true;
}

bool broiler_cpu_emulate_io(struct broiler_cpu *vcpu, u16 port, void *data,
				int direction, int size, u32 count)
{
	struct mmio_mapping *mmio;
	bool is_write = direction == KVM_EXIT_IO_OUT;

	mmio = mmio_get(&pio_tree, port, size);
	if (!mmio)
		return true;

	while (count--) {
		mmio->mmio_fn(vcpu, port, data, size, is_write, mmio->ptr);

		data += size;
	}

	mmio_put(vcpu->broiler, &pio_tree, mmio);

	return true;
}

bool broiler_ioport_deregister(struct broiler *broiler,
				u64 phys_addr, unsigned int flags)
{
	struct mmio_mapping *mmio;
	struct rb_root *tree;

	if (ioport_is_mmio(flags))
		tree = &mmio_tree;
	else
		tree = &pio_tree;

	mutex_lock(&mmio_lock);
	mmio = mmio_search_single(tree, phys_addr);
	if (mmio == NULL) {
		mutex_unlock(&mmio_lock);
		return false;
	}

	/*
	 * The PCI emulation code calls this function when memroy access is
	 * disabled for a device, or when a BAR has a new address assigned.
	 * PCI emulation doesn't use any locks and as a result we can end
	 * up in a situation where we have called mmio_get() to do emulation
	 * on one VCPU thread (let's call it VCPU0), and several other VCPU
	 * threads have called broiler_deregister_mmio(). In this case, if
	 * we decrement refcount kvm_deregister_mmio() (eigher directly, or
	 * by calling mmio_put()), refcount will reach 0 and we will free
	 * the mmio node before VCPU0 has call mmio_put(). This will trigger
	 * use-after-free errors on VPU0.
	 */
	if (mmio->refcount == 0)
		mmio_deregister(broiler, tree, mmio);
	else
		mmio->remove = true;
	mutex_unlock(&mmio_lock);

	return true;
}

int broiler_ioport_setup(struct broiler *broiler)
{
	int r;

	/* Legacy ioport setup */

	/* 0000 - 001F - DMA1 controller */
	r = broiler_register_pio(broiler, 0x0000, 32, dummy_io, NULL);
	if (r < 0)
		return r;

	/* 0x0020 - 0x003F - 8259A PIC 1 */
	r = broiler_register_pio(broiler, 0x0020, 2, dummy_io, NULL);
	if (r < 0)
		return r;

	/* PORT 0040 - 005F - PIT - PROGRAMMABLE INTERVAL TIMER (8253, 8254) */
	r = broiler_register_pio(broiler, 0x0040, 4, dummy_io, NULL);
	if (r < 0)
		return r;

	/* 0092 - PS/2 system control port A */
	r = broiler_register_pio(broiler, 0x0092, 1, ps2_control_io, NULL);
	if (r < 0)
		return r;

	/* 0x00A0 - 0x00AF - 8259A PIC 2 */
	r = broiler_register_pio(broiler, 0x00A0, 2, dummy_io, NULL);
	if (r < 0)
		return r;

	/* 00C0 - 001F - DMA2 controller */
	r = broiler_register_pio(broiler, 0x00C0, 32, dummy_io, NULL);
	if (r < 0)
		return r;

	/* PORT 00E0 - 00EF are 'motherboard specific' so we use them
	 * for our internal debugging purposes. */
	r = broiler_register_pio(broiler, 0x00e0, 1, debug_io, NULL);
	if (r < 0)
		return r;

	/* PORT 00ED - DUMMY PORT FOR DELAY */
	r = broiler_register_pio(broiler, 0x00ed, 1, dummy_io, NULL);
	if (r < 0)
		return r;

	/* 0x00F0 - 0x00FF - Math co-processor */
	r = broiler_register_pio(broiler, 0x00f0, 2, dummy_io, NULL);
	if (r < 0)
		return r;

	/* PORT 0278 - 027A - PARALLEL PRINTER PORT 
	 * (usually LPT1, sometimes LPT2) */
	r = broiler_register_pio(broiler, 0x0278, 3, dummy_io, NULL);
	if (r < 0)
		return r;

	/* PORT 0378 - 037A - PARALLEL PRINTER PORT
	 * (usually LPT2, sometime LPT3) */
	r = broiler_register_pio(broiler, 0x0378, 3, dummy_io, NULL);
	if (r < 0)
		return r;

	/* PORT 03D4 - 03D5 - COLOR VIDEO - CRT CONTROL REGISTERS */
	r = broiler_register_pio(broiler, 0x03d4, 1, dummy_io, NULL);
	if (r < 0)
		return r;
	r = broiler_register_pio(broiler, 0x03d5, 1, dummy_io, NULL);
	if (r < 0)
		return r;

	r = broiler_register_pio(broiler, 0x0402, 1, dummy_io, NULL);
	if (r < 0)
		return r;

	/* 0510 - Broiler BIOS configuration register */
	r = broiler_register_pio(broiler, 0x0510, 2, dummy_io, NULL);
	if (r < 0)
		return r;

	return 0;
}

void broiler_ioport_exit(struct broiler *broiler) { }
