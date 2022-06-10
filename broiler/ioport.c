
#include "broiler/broiler.h"
#include "broiler/kvm.h"
#include "broiler/ioport.h"
#include "linux/mutex.h"
#include "linux/rbtree.h"

static DEFINE_MUTEX(mmio_lock);

static struct rb_root mmio_tree = RB_ROOT;
static struct rb_root pio_tree = RB_ROOT;

static void dummy_io(struct kvm_cpu *vcpu, u64 addr, 
			u8 *data, u32 len, u8 is_write, void *ptr) { }

static int mmio_insert(struct rb_root *root, struct mmio_mapping *data)
{
	rb_int_insert(root, &data->node);
}

static bool ioport_is_mmio(unsigned int flags)
{
	return (flags & IOPORT_BUS_MASK) == DEVICE_BUS_MMIO;
}

/* The 'fast A20 gate' */

static void ps2_control_io(struct kvm_cpu *vcpu, u64 addr, u8 *data,
				u32 len, u8 is_write, void *ptr)
{
	/* A20 is always enabled */
	if (!is_write)
		ioport_write8(data, 0x02);
}

static void debug_io(struct kvm_cpu *vcpu, u64 addr, u8 *data,
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
		if (ioctl(broiler->vm_fd, KVM_REGISTER_COALESCED_MMIO, &zone)) {
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
