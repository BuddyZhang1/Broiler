// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_VIRTIO_H
#define _BROILER_VIRTIO_H

#include <pthread.h>
#include "linux/mutex.h"
#include "linux/list.h"
#include "broiler/disk.h"
#include "broiler/pci.h"
#include "broiler/device.h"
#include "broiler/utils.h"
#include "broiler/memory.h"
#include "broiler/types.h"
#ifndef __KERNEL__
#include <stdint.h>
#endif

/* This header, excluding the #ifdef __KERNEL__ part, is BSD licensed so
 * anyone can use the definitions to implement compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

/* Virtio devices use a standardized configuration space to define their
 * features and pass configuration information, but each implementation can
 * store and access that space differently. */

/* Status byte for guest to report progress, and synchronize features. */
/* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	1
/* We have found a driver for the device. */
#define VIRTIO_CONFIG_S_DRIVER		2
/* Driver has used its parts of the config, and is happy */
#define VIRTIO_CONFIG_S_DRIVER_OK	4
/* Driver has finished configuring features */
#define VIRTIO_CONFIG_S_FEATURES_OK	8
/* Device entered invalid state, driver must reset it */
#define VIRTIO_CONFIG_S_NEEDS_RESET	0x40
/* We've given up on this device. */
#define VIRTIO_CONFIG_S_FAILED		0x80

/*
 * Virtio feature bits VIRTIO_TRANSPORT_F_START through
 * VIRTIO_TRANSPORT_F_END are reserved for the transport
 * being used (e.g. virtio_ring, virtio_pci etc.), the
 * rest are per-device feature bits.
 */     
#define VIRTIO_TRANSPORT_F_START	28
#define VIRTIO_TRANSPORT_F_END		38

#ifndef VIRTIO_CONFIG_NO_LEGACY
/* Do we get callbacks when the ring is completely used, even if we've
 * suppressed them? */
#define VIRTIO_F_NOTIFY_ON_EMPTY	24
   
/* Can the device handle any descriptor layout? */
#define VIRTIO_F_ANY_LAYOUT		27
#endif /* VIRTIO_CONFIG_NO_LEGACY */

/* v1.0 compliant. */
#define VIRTIO_F_VERSION_1		32

/*
 * If clear - device has the platform DMA (e.g. IOMMU) bypass quirk feature.
 * If set - use platform DMA tools to access the memory.
 *
 * Note the reverse polarity (compared to most other features),
 * this is for compatibility with legacy systems.
 */
#define VIRTIO_F_ACCESS_PLATFORM	33
#ifndef __KERNEL__
/* Legacy name for VIRTIO_F_ACCESS_PLATFORM (for compatibility with old userspace) */
#define VIRTIO_F_IOMMU_PLATFORM		VIRTIO_F_ACCESS_PLATFORM
#endif /* __KERNEL__ */

/* This feature indicates support for the packed virtqueue layout. */
#define VIRTIO_F_RING_PACKED		34

/*
 * Inorder feature indicates that all buffers are used by the device
 * in the same order in which they have been made available.
 */
#define VIRTIO_F_IN_ORDER		35

/*
 * This feature indicates that memory accesses by the driver and the
 * device are ordered in a way described by the platform.
 */
#define VIRTIO_F_ORDER_PLATFORM		36

/*
 * Does the device support Single Root I/O Virtualization?
 */
#define VIRTIO_F_SR_IOV			37

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT		1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE		2
/* This means the buffer contains a list of buffer descriptors. */
#define VRING_DESC_F_INDIRECT		4

/*
 * Mark a descriptor as available or used in packed ring.
 * Notice: they are defined as shifts instead of shifted values.
 */
#define VRING_PACKED_DESC_F_AVAIL	7
#define VRING_PACKED_DESC_F_USED	15

/* The Host uses this in used->flags to advise the Guest: don't kick me when
 * you add a buffer.  It's unreliable, so it's simply an optimization.  Guest
 * will still kick if it's out of buffers. */
#define VRING_USED_F_NO_NOTIFY		1
/* The Guest uses this in avail->flags to advise the Host: don't interrupt me
 * when you consume a buffer.  It's unreliable, so it's simply an
 * optimization.  */
#define VRING_AVAIL_F_NO_INTERRUPT	1

/* Enable events in packed ring. */
#define VRING_PACKED_EVENT_FLAG_ENABLE	0x0
/* Disable events in packed ring. */
#define VRING_PACKED_EVENT_FLAG_DISABLE	0x1
/*
 * Enable events for a specific descriptor in packed ring.
 * (as specified by Descriptor Ring Change Event Offset/Wrap Counter).
 * Only valid if VIRTIO_RING_F_EVENT_IDX has been negotiated.
 */
#define VRING_PACKED_EVENT_FLAG_DESC	0x2

/*
 * Wrap counter bit shift in event suppression structure
 * of packed ring.
 */
#define VRING_PACKED_EVENT_F_WRAP_CTR	15

/* We support indirect buffer descriptors */
#define VIRTIO_RING_F_INDIRECT_DESC	28

/* The Guest publishes the used index for which it expects an interrupt
 * at the end of the avail ring. Host should ignore the avail->flags field. */
/* The Host publishes the avail index for which it expects a kick
 * at the end of the used ring. Guest should ignore the used->flags field. */
#define VIRTIO_RING_F_EVENT_IDX		29

#define VIRTIO_ENDIAN_LE	(1 << 0)
#define VIRTIO_ENDIAN_HOST	VIRTIO_ENDIAN_LE

/* Alignment requirements for vring elements.
 * When using pre-virtio 1.0 layout, these fall out naturally.
 */
#define VRING_AVAIL_ALIGN_SIZE		2
#define VRING_USED_ALIGN_SIZE		4
#define VRING_DESC_ALIGN_SIZE		16

/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
struct vring_desc {
	/* Address (guest-physical). */
	__virtio64 addr;
	/* Length. */
	__virtio32 len;
	/* The flags as indicated above. */
	__virtio16 flags;
	/* We chain unused descriptors via this, too */
	__virtio16 next;
};

struct vring_avail {
	__virtio16 flags;
	__virtio16 idx;
	__virtio16 ring[];
};

/* u32 is used here for ids for padding reasons. */
struct vring_used_elem {
	/* Index of start of used descriptor chain. */
	__virtio32 id;
	/* Total length of the descriptor chain which was used (written to) */
	__virtio32 len;
};

typedef struct vring_used_elem __attribute__((aligned(VRING_USED_ALIGN_SIZE)))
	vring_used_elem_t;

struct vring_used {
	__virtio16 flags;
	__virtio16 idx;
	vring_used_elem_t ring[];
};

/*
 * The ring element addresses are passed between components with different
 * alignments assumptions. Thus, we might need to decrease the compiler-selected
 * alignment, and so must use a typedef to make sure the aligned attribute
 * actually takes hold:
 *
 * https://gcc.gnu.org/onlinedocs//gcc/Common-Type-Attributes.html#Common-Type-Attributes
 *
 * When used on a struct, or struct member, the aligned attribute can only
 * increase the alignment; in order to decrease it, the packed attribute must
 * be specified as well. When used as part of a typedef, the aligned attribute
 * can both increase and decrease alignment, and specifying the packed
 * attribute generates a warning.
 */
typedef struct vring_desc __attribute__((aligned(VRING_DESC_ALIGN_SIZE)))
	vring_desc_t;
typedef struct vring_avail __attribute__((aligned(VRING_AVAIL_ALIGN_SIZE)))
	vring_avail_t;
typedef struct vring_used __attribute__((aligned(VRING_USED_ALIGN_SIZE)))
	vring_used_t;

struct vring {
	unsigned int num;
	vring_desc_t *desc;
	vring_avail_t *avail;
	vring_used_t *used;
};

#ifndef VIRTIO_RING_NO_LEGACY

/* The standard layout for the ring is a continuous chunk of memory which looks
 * like this.  We assume num is a power of 2.
 *
 * struct vring
 * {
 *      // The actual descriptors (16 bytes each)
 *      struct vring_desc desc[num];
 *
 *      // A ring of available descriptor heads with free-running index.
 *      __virtio16 avail_flags;
 *      __virtio16 avail_idx;
 *      __virtio16 available[num];
 *      __virtio16 used_event_idx;
 *
 *      // Padding to the next align boundary.
 *      char pad[];
 *
 *      // A ring of used descriptor heads with free-running index.
 *      __virtio16 used_flags;
 *      __virtio16 used_idx;
 *      struct vring_used_elem used[num];
 *      __virtio16 avail_event_idx;
 * };
 */
/* We publish the used event index at the end of the available ring, and vice
 * versa. They are at the end for backwards compatibility. */
#define vring_used_event(vr) ((vr)->avail->ring[(vr)->num])
#define vring_avail_event(vr) (*(__virtio16 *)&(vr)->used->ring[(vr)->num])

static inline void vring_init(struct vring *vr, unsigned int num, void *p,
                              unsigned long align)
{
	vr->num = num;
	vr->desc = p;
	vr->avail = (struct vring_avail *)((char *)p + num * sizeof(struct vring_desc));
	vr->used = (void *)(((uintptr_t)&vr->avail->ring[num] + sizeof(__virtio16)
				+ align-1) & ~(align - 1));
}

static inline unsigned vring_size(unsigned int num, unsigned long align)
{
	return ((sizeof(struct vring_desc) * num + sizeof(__virtio16) * (3 + num)
		+ align - 1) & ~(align - 1))
		+ sizeof(__virtio16) * 3 + sizeof(struct vring_used_elem) * num;
}

#endif /* VIRTIO_RING_NO_LEGACY */

/* The following is used with USED_EVENT_IDX and AVAIL_EVENT_IDX */
/* Assuming a given event_idx value from the other side, if
 * we have just incremented index from old to new_idx,
 * should we trigger an event? */
static inline int vring_need_event(__u16 event_idx, __u16 new_idx, __u16 old)
{
	/* Note: Xen has similar logic for notification hold-off
	 * in include/xen/interface/io/ring.h with req_event and req_prod
	 * corresponding to event_idx + 1 and new_idx respectively.
	 * Note also that req_event and req_prod in Xen start at 1,
	 * event indexes in virtio start at 0. */
	return (__u16)(new_idx - event_idx - 1) < (__u16)(new_idx - old);
}

struct vring_packed_desc_event {
	/* Descriptor Ring Change Event Offset/Wrap Counter. */
	__le16 off_wrap;
	/* Descriptor Ring Change Event Flags. */
	__le16 flags;
};

struct vring_packed_desc {
	/* Buffer Address. */
	__le64 addr;
	/* Buffer Length. */
	__le32 len;
	/* Buffer ID. */
	__le16 id;
	/* The flags depending on descriptor type. */
	__le16 flags;
};

#ifndef VIRTIO_PCI_NO_LEGACY

/* A 32-bit r/o bitmask of the features supported by the host */
#define VIRTIO_PCI_HOST_FEATURES	0

/* A 32-bit r/w bitmask of features activated by the guest */
#define VIRTIO_PCI_GUEST_FEATURES	4

/* A 32-bit r/w PFN for the currently selected queue */
#define VIRTIO_PCI_QUEUE_PFN		8

/* A 16-bit r/o queue size for the currently selected queue */
#define VIRTIO_PCI_QUEUE_NUM		12

/* A 16-bit r/w queue selector */
#define VIRTIO_PCI_QUEUE_SEL		14

/* A 16-bit r/w queue notifier */
#define VIRTIO_PCI_QUEUE_NOTIFY		16

/* An 8-bit device status register.  */
#define VIRTIO_PCI_STATUS		18

/* An 8-bit r/o interrupt status register.  Reading the value will return the
 * current contents of the ISR and will also clear it.  This is effectively
 * a read-and-acknowledge. */
#define VIRTIO_PCI_ISR			19

/* MSI-X registers: only enabled if MSI-X is enabled. */
/* A 16-bit vector for configuration changes. */
#define VIRTIO_MSI_CONFIG_VECTOR	20
/* A 16-bit vector for selected queue notifications. */
#define VIRTIO_MSI_QUEUE_VECTOR		22

/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_PCI_CONFIG_OFF(msix_enabled)	((msix_enabled) ? 24 : 20)
/* Deprecated: please use VIRTIO_PCI_CONFIG_OFF instead */
#define VIRTIO_PCI_CONFIG(dev)  VIRTIO_PCI_CONFIG_OFF((dev)->msix_enabled)

/* Virtio ABI version, this must match exactly */
#define VIRTIO_PCI_ABI_VERSION		0

/* How many bits to shift physical queue address written to QUEUE_PFN.
 * 12 is historical, and due to x86 page size. */
#define VIRTIO_PCI_QUEUE_ADDR_SHIFT	12

/* The alignment to use between consumer and producer parts of vring.
 * x86 pagesize again. */
#define VIRTIO_PCI_VRING_ALIGN		4096

#endif /* VIRTIO_PCI_NO_LEGACY */

/* The bit of the ISR which indicates a device configuration change. */
#define VIRTIO_PCI_ISR_CONFIG		0x2
/* Vector value used to disable MSI for queue */
#define VIRTIO_MSI_NO_VECTOR		0xffff

#ifndef VIRTIO_PCI_NO_MODERN

/* IDs for different capabilities.  Must all exist. */

/* Common configuration */
#define VIRTIO_PCI_CAP_COMMON_CFG	1
/* Notifications */
#define VIRTIO_PCI_CAP_NOTIFY_CFG	2
/* ISR access */
#define VIRTIO_PCI_CAP_ISR_CFG		3
/* Device specific configuration */
#define VIRTIO_PCI_CAP_DEVICE_CFG	4
/* PCI configuration access */
#define VIRTIO_PCI_CAP_PCI_CFG		5
/* Additional shared memory capability */
#define VIRTIO_PCI_CAP_SHARED_MEMORY_CFG 8

/* This is the PCI capability header: */
struct virtio_pci_cap {
	__u8 cap_vndr;          /* Generic PCI field: PCI_CAP_ID_VNDR */
	__u8 cap_next;          /* Generic PCI field: next ptr. */
	__u8 cap_len;           /* Generic PCI field: capability length */
	__u8 cfg_type;          /* Identifies the structure. */
	__u8 bar;               /* Where to find it. */
	__u8 id;                /* Multiple capabilities of the same type */
	__u8 padding[2];        /* Pad to full dword. */
	__le32 offset;          /* Offset within bar. */
	__le32 length;          /* Length of the structure, in bytes. */
};

struct virtio_pci_cap64 {
	struct virtio_pci_cap cap;
	__le32 offset_hi;             /* Most sig 32 bits of offset */
	__le32 length_hi;             /* Most sig 32 bits of length */
};

struct virtio_pci_notify_cap {
	struct virtio_pci_cap cap;
	__le32 notify_off_multiplier;   /* Multiplier for queue_notify_off. */
};

/* Fields in VIRTIO_PCI_CAP_COMMON_CFG: */
struct virtio_pci_common_cfg {
	/* About the whole device. */
	__le32 device_feature_select;   /* read-write */
	__le32 device_feature;          /* read-only */
	__le32 guest_feature_select;    /* read-write */
	__le32 guest_feature;           /* read-write */
	__le16 msix_config;             /* read-write */
	__le16 num_queues;              /* read-only */
	__u8 device_status;             /* read-write */
	__u8 config_generation;         /* read-only */

	/* About a specific virtqueue. */
	__le16 queue_select;            /* read-write */
	__le16 queue_size;              /* read-write, power of 2. */
	__le16 queue_msix_vector;       /* read-write */
	__le16 queue_enable;            /* read-write */
	__le16 queue_notify_off;        /* read-only */
	__le32 queue_desc_lo;           /* read-write */
	__le32 queue_desc_hi;           /* read-write */
	__le32 queue_avail_lo;          /* read-write */
	__le32 queue_avail_hi;          /* read-write */
	__le32 queue_used_lo;           /* read-write */
	__le32 queue_used_hi;           /* read-write */
};

/* Fields in VIRTIO_PCI_CAP_PCI_CFG: */
struct virtio_pci_cfg_cap {
	struct virtio_pci_cap cap;
	__u8 pci_cfg_data[4]; /* Data for BAR access. */
};

/* Macro versions of offsets for the Old Timers! */
#define VIRTIO_PCI_CAP_VNDR		0
#define VIRTIO_PCI_CAP_NEXT		1
#define VIRTIO_PCI_CAP_LEN		2
#define VIRTIO_PCI_CAP_CFG_TYPE		3
#define VIRTIO_PCI_CAP_BAR		4
#define VIRTIO_PCI_CAP_OFFSET		8
#define VIRTIO_PCI_CAP_LENGTH		12
#define VIRTIO_PCI_NOTIFY_CAP_MULT	16
#define VIRTIO_PCI_COMMON_DFSELECT	0
#define VIRTIO_PCI_COMMON_DF		4
#define VIRTIO_PCI_COMMON_GFSELECT	8
#define VIRTIO_PCI_COMMON_GF		12
#define VIRTIO_PCI_COMMON_MSIX		16
#define VIRTIO_PCI_COMMON_NUMQ		18
#define VIRTIO_PCI_COMMON_STATUS	20
#define VIRTIO_PCI_COMMON_CFGGENERATION	21
#define VIRTIO_PCI_COMMON_Q_SELECT	22
#define VIRTIO_PCI_COMMON_Q_SIZE	24
#define VIRTIO_PCI_COMMON_Q_MSIX	26
#define VIRTIO_PCI_COMMON_Q_ENABLE	28
#define VIRTIO_PCI_COMMON_Q_NOFF	30
#define VIRTIO_PCI_COMMON_Q_DESCLO	32
#define VIRTIO_PCI_COMMON_Q_DESCHI	36
#define VIRTIO_PCI_COMMON_Q_AVAILLO	40
#define VIRTIO_PCI_COMMON_Q_AVAILHI	44
#define VIRTIO_PCI_COMMON_Q_USEDLO	48
#define VIRTIO_PCI_COMMON_Q_USEDHI	52

#endif /* VIRTIO_PCI_NO_MODERN */

/* Feature bits */
#define VIRTIO_BLK_F_SIZE_MAX	1       /* Indicates maximum segment size */
#define VIRTIO_BLK_F_SEG_MAX	2       /* Indicates maximum # of segments */
#define VIRTIO_BLK_F_GEOMETRY	4       /* Legacy geometry available  */
#define VIRTIO_BLK_F_RO		5       /* Disk is read-only */
#define VIRTIO_BLK_F_BLK_SIZE	6       /* Block size of disk is available*/
#define VIRTIO_BLK_F_TOPOLOGY	10      /* Topology information is available */
#define VIRTIO_BLK_F_MQ		12      /* support more than one vq */
#define VIRTIO_BLK_F_DISCARD	13      /* DISCARD is supported */
#define VIRTIO_BLK_F_WRITE_ZEROES	14      /* WRITE ZEROES is supported */

/* Legacy feature bits */
#ifndef VIRTIO_BLK_NO_LEGACY
#define VIRTIO_BLK_F_BARRIER	0       /* Does host support barriers? */
#define VIRTIO_BLK_F_SCSI	7       /* Supports scsi command passthru */
#define VIRTIO_BLK_F_FLUSH	9       /* Flush command supported */
#define VIRTIO_BLK_F_CONFIG_WCE	11      /* Writeback mode available in config */
#ifndef __KERNEL__
/* Old (deprecated) name for VIRTIO_BLK_F_FLUSH. */
#define VIRTIO_BLK_F_WCE	VIRTIO_BLK_F_FLUSH
#endif
#endif /* !VIRTIO_BLK_NO_LEGACY */

#define VIRTIO_BLK_ID_BYTES	20      /* ID string length */

struct virtio_blk_config {
	/* The capacity (in 512-byte sectors). */
	__virtio64 capacity;
	/* The maximum segment size (if VIRTIO_BLK_F_SIZE_MAX) */
	__virtio32 size_max;
	/* The maximum number of segments (if VIRTIO_BLK_F_SEG_MAX) */
	__virtio32 seg_max;
	/* geometry of the device (if VIRTIO_BLK_F_GEOMETRY) */
	struct virtio_blk_geometry {
		__virtio16 cylinders;
		__u8 heads;
		__u8 sectors;
	} geometry;

	/* block size of device (if VIRTIO_BLK_F_BLK_SIZE) */
	__virtio32 blk_size;

	/* the next 4 entries are guarded by VIRTIO_BLK_F_TOPOLOGY  */
	/* exponent for physical block per logical block. */
	__u8 physical_block_exp;
	/* alignment offset in logical blocks. */
	__u8 alignment_offset;
	/* minimum I/O size without performance penalty in logical blocks. */
	__virtio16 min_io_size;
	/* optimal sustained I/O size in logical blocks. */
	__virtio32 opt_io_size;

	/* writeback mode (if VIRTIO_BLK_F_CONFIG_WCE) */
	__u8 wce;
	__u8 unused;

	/* number of vqs, only available when VIRTIO_BLK_F_MQ is set */
	__virtio16 num_queues;

	/* the next 3 entries are guarded by VIRTIO_BLK_F_DISCARD */
	/*
	 * The maximum discard sectors (in 512-byte sectors) for
	 * one segment.
	 */
	__virtio32 max_discard_sectors;
	/*
	 * The maximum number of discard segments in a
	 * discard command.
	 */
	__virtio32 max_discard_seg;
	/* Discard commands must be aligned to this number of sectors. */
	__virtio32 discard_sector_alignment;

	/* the next 3 entries are guarded by VIRTIO_BLK_F_WRITE_ZEROES */
	/*
	 * The maximum number of write zeroes sectors (in 512-byte sectors) in
	 * one segment.
	 */
	__virtio32 max_write_zeroes_sectors;
	/*
	 * The maximum number of segments in a write zeroes
	 * command.
	 */
	__virtio32 max_write_zeroes_seg;
	/*
	 * Set if a VIRTIO_BLK_T_WRITE_ZEROES request may result in the
	 * deallocation of one or more of the sectors.
	 */
	__u8 write_zeroes_may_unmap;

	__u8 unused1[3];
} __attribute__((packed));

/*
 * Command types
 *
 * Usage is a bit tricky as some bits are used as flags and some are not.
 *
 * Rules:
 *   VIRTIO_BLK_T_OUT may be combined with VIRTIO_BLK_T_SCSI_CMD or
 *   VIRTIO_BLK_T_BARRIER.  VIRTIO_BLK_T_FLUSH is a command of its own
 *   and may not be combined with any of the other flags.
 */

/* These two define direction. */
#define VIRTIO_BLK_T_IN		0
#define VIRTIO_BLK_T_OUT	1

#ifndef VIRTIO_BLK_NO_LEGACY
/* This bit says it's a scsi command, not an actual read or write. */
#define VIRTIO_BLK_T_SCSI_CMD	2
#endif /* VIRTIO_BLK_NO_LEGACY */

/* Cache flush command */
#define VIRTIO_BLK_T_FLUSH	4

/* Get device ID command */
#define VIRTIO_BLK_T_GET_ID	8

/* Discard command */
#define VIRTIO_BLK_T_DISCARD	11

/* Write zeroes command */
#define VIRTIO_BLK_T_WRITE_ZEROES	13

#ifndef VIRTIO_BLK_NO_LEGACY
/* Barrier before this op. */
#define VIRTIO_BLK_T_BARRIER	0x80000000
#endif /* !VIRTIO_BLK_NO_LEGACY */

/*
 * This comes first in the read scatter-gather list.
 * For legacy virtio, if VIRTIO_F_ANY_LAYOUT is not negotiated,
 * this is the first element of the read scatter-gather list.
 */
struct virtio_blk_outhdr {
	/* VIRTIO_BLK_T* */
	__virtio32 type;
	/* io priority. */
	__virtio32 ioprio;
	/* Sector (ie. 512 byte offset) */
	__virtio64 sector;
};

/* Unmap this range (only valid for write zeroes command) */
#define VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP	0x00000001

/* Discard/write zeroes range for each request. */
struct virtio_blk_discard_write_zeroes {
	/* discard/write zeroes start sector */
	__le64 sector;
	/* number of discard/write zeroes sectors */
	__le32 num_sectors;
	/* flags for this range */
	__le32 flags;
};

#ifndef VIRTIO_BLK_NO_LEGACY
struct virtio_scsi_inhdr {
	__virtio32 errors;
	__virtio32 data_len;
	__virtio32 sense_len;
	__virtio32 residual;
};
#endif /* !VIRTIO_BLK_NO_LEGACY */

/* And this is the final byte of the write scatter-gather list. */
#define VIRTIO_BLK_S_OK		0
#define VIRTIO_BLK_S_IOERR	1
#define VIRTIO_BLK_S_UNSUPP	2

#define VIRTIO_IRQ_LOW		0
#define VIRTIO_IRQ_HIGH		1
#define VIRTIO_PCI_O_CONFIG	0
#define VIRTIO_PCI_O_MSIX	1

/* Reserved status bits */
#define VIRTIO_CONFIG_S_MASK \
	(VIRTIO_CONFIG_S_ACKNOWLEDGE |  \
	VIRTIO_CONFIG_S_DRIVER |        \
	VIRTIO_CONFIG_S_DRIVER_OK |     \
	VIRTIO_CONFIG_S_FEATURES_OK |   \
	VIRTIO_CONFIG_S_FAILED)

/* Start the device */
#define VIRTIO_STATUS_START		(1 << 8)
/* Stop the device */
#define VIRTIO_STATUS_STOP		(1 << 9)
/* Initialize the config */
#define VIRTIO_STATUS_CONFIG		(1 << 10)

/*
 * Virtio IDs
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */
                
#define VIRTIO_ID_NET		1 /* virtio net */
#define VIRTIO_ID_BLOCK		2 /* virtio block */
#define VIRTIO_ID_CONSOLE	3 /* virtio console */
#define VIRTIO_ID_RNG		4 /* virtio rng */
#define VIRTIO_ID_BALLOON	5 /* virtio balloon */
#define VIRTIO_ID_RPMSG		7 /* virtio remote processor messaging */
#define VIRTIO_ID_SCSI		8 /* virtio scsi */
#define VIRTIO_ID_9P		9 /* 9p virtio console */
#define VIRTIO_ID_RPROC_SERIAL	11 /* virtio remoteproc serial link */
#define VIRTIO_ID_CAIF		12 /* Virtio caif */
#define VIRTIO_ID_INPUT		18 /* virtio input */
#define VIRTIO_ID_VSOCK		19 /* virtio vsock transport */

/*      
 * Virtio PCI device constants and resources
 * they do use (such as irqs and pins).
 */     

#define PCI_DEVICE_ID_VIRTIO_NET		0x1000
#define PCI_DEVICE_ID_VIRTIO_BLK		0x1001
#define PCI_DEVICE_ID_VIRTIO_CONSOLE		0x1003
#define PCI_DEVICE_ID_VIRTIO_RNG		0x1004
#define PCI_DEVICE_ID_VIRTIO_BLN		0x1005
#define PCI_DEVICE_ID_VIRTIO_SCSI		0x1008
#define PCI_DEVICE_ID_VIRTIO_9P			0x1009
#define PCI_DEVICE_ID_VIRTIO_VSOCK		0x1012
#define PCI_DEVICE_ID_VESA			0x2000 
#define PCI_DEVICE_ID_PCI_SHMEM			0x0001
                
#define PCI_CLASS_BLK				0x018000
#define PCI_CLASS_NET				0x020000
#define PCI_CLASS_CONSOLE			0x078000
/*
 * 0xFF Device does not fit in any defined classes
 */
#define PCI_CLASS_RNG				0xff0000
#define PCI_CLASS_BLN				0xff0000
#define PCI_CLASS_9P				0xff0000
#define PCI_CLASS_VSOCK				0xff0000

/* VRING */

#define vring_avail_event(vr)	(*(__virtio16 *)&(vr)->used->ring[(vr)->num])

enum virtio_trans {
	VIRTIO_PCI,
	VIRTIO_MMIO,
};

#define VIRTIO_DEFAULT_TRANS(broiler)	VIRTIO_PCI

struct virtio_device {
	bool				legacy;
	bool				use_vhost;
	void				*virtio;
	struct virtio_ops		*ops;
	u16				endian;
	u32				features;
	u32				status;
};

struct virtio_ops {
	u8 *(*get_config)(struct broiler *broiler, void *dev);
	size_t (*get_config_size)(struct broiler *broiler, void *dev);
	u32 (*get_host_features)(struct broiler *broiler, void *dev);
	void (*set_guest_features)(struct broiler *broiler, void *dev, u32 features);
	int (*get_vq_count)(struct broiler *broiler, void *dev);
	int (*init_vq)(struct broiler *broiler, void *dev, u32 vq, u32 page_size,
					u32 align, u32 pfn);
	void (*exit_vq)(struct broiler *broiler, void *dev, u32 vq);
	int (*notify_vq)(struct broiler *broiler, void *dev, u32 vq);
	struct virt_queue *(*get_vq)(struct broiler *broiler, void *dev, u32 vq);
	int (*get_size_vq)(struct broiler *broiler, void *dev, u32 vq);
	int (*set_size_vq)(struct broiler *broiler, void *dev, u32 vq, int size);
	void (*notify_vq_gsi)(struct broiler *broiler, void *dev, u32 vq, u32 gsi);
	void (*notify_vq_eventfd)(struct broiler *broiler, void *dev, u32 vq, u32 efd);
	int (*signal_vq)(struct broiler *broiler, struct virtio_device *vdev, u32 queueid);
	int (*signal_config)(struct broiler *broiler, struct virtio_device *vdev);
	void (*notify_status)(struct broiler *broiler, void *dev, u32 status);
	int (*init)(struct broiler *broiler, void *dev, struct virtio_device *vdev,
				int device_id, int subsys_id, int class);
	int (*exit)(struct broiler *broiler, struct virtio_device *vdev);
	int (*reset)(struct broiler *broiler, struct virtio_device *vdev);
};

struct virt_queue {
	struct vring	vring;
	u32		pfn; 
	/* The last_avail_idx field is an index to ->ring of struct vring_avail.
	   It's where we assume the next request index is at.  */
	u16		last_avail_idx;
	u16		last_used_signalled;
	u16		endian;
	bool		use_event_idx;
	bool		enabled;
};

#define virtio_guest_to_host_u16(x, v)		(v)
#define virtio_host_to_guest_u16(x, v)		(v)
#define virtio_guest_to_host_u32(x, v)		(v)
#define virtio_host_to_guest_u32(x, v)		(v)
#define virtio_guest_to_host_u64(x, v)		(v)
#define virtio_host_to_guest_u64(x, v)		(v)

static inline void  *
virtio_get_vq(struct broiler *broiler, u32 pfn, u32 page_size)
{
	return gpa_flat_to_hva(broiler, (u64)pfn * page_size);
}

static inline void
virtio_init_device_vq(struct virtio_device *vdev, struct virt_queue *vq)
{
	vq->endian = vdev->endian;
	vq->use_event_idx = (vdev->features & VIRTIO_RING_F_EVENT_IDX);
	vq->enabled = true;
}

static inline u16 virt_queue_pop(struct virt_queue *queue)
{
	u16 guest_idx;

	/*
	 * The guest updates the avail index after writing the ring entry.
	 * Ensure that we read the updated entry once virt_queue_available()
	 * observes the new index.
	 */
	rmb();

	guest_idx = queue->vring.avail->ring[queue->last_avail_idx++ %
							queue->vring.num];

	return virtio_guest_to_host_u16(queue, guest_idx);
}

static inline bool virt_queue_available(struct virt_queue *vq)
{
	u16 last_avail_idx = virtio_host_to_guest_u16(vq, vq->last_avail_idx);

	if (!vq->vring.avail)
		return 0;

	if (vq->use_event_idx) {
		vring_avail_event(&vq->vring) = last_avail_idx;
		/*
		 * After the driver writes a new avail index, it reads the event
		 * index to see if we need any notification. Ensure that it
		 * reads the updated index, or else we'll miss the 
		 * notification.
		 */
		mb();
	}

	return vq->vring.avail->idx != last_avail_idx;
}

/* VIRTIO-PCI */
#define VIRTIO_PCI_MAX_VQ	32
#define VIRTIO_PCI_MAX_CONFIG	1
#define VIRTIO_PCI_F_SIGNAL_MSI	(1 << 0)

#define ALIGN_UP(x, s)		ALIGN((x) + (s) - 1, (s))
#define VIRTIO_NR_MSIX		(VIRTIO_PCI_MAX_VQ + VIRTIO_PCI_MAX_CONFIG)
#define VIRTIO_MSIX_TABLE_SIZE	(VIRTIO_NR_MSIX * 16)
#define VIRTIO_MSIX_PBA_SIZE	(ALIGN_UP(VIRTIO_MSIX_TABLE_SIZE, 64) / 8)
#define VIRTIO_MSIX_BAR_SIZE	(1UL << fls_long(VIRTIO_MSIX_TABLE_SIZE + \
						 VIRTIO_MSIX_PBA_SIZE))

struct virtio_pci_ioevent_param {
	struct virtio_device	*vdev;
	u32			vq;
};

struct virtio_pci {
	struct pci_device	pdev;
	struct device		dev;        
	void			*data;
	struct broiler		*broiler;
        
	u8			status;
	u8			isr;
	u32			features;

	/*      
	 * We cannot rely on the INTERRUPT_LINE byte in the config space once
	 * we have run guest code, as the OS is allowed to use that field
	 * as a scratch pad to communicate between driver and PCI layer.
	 * So store our legacy interrupt line number in here for internal use.
	 */     
	u8			legacy_irq_line;
        
	/* MSI-X */
	u16			config_vector;
	u32			config_gsi;
	u32			vq_vector[VIRTIO_PCI_MAX_VQ];
	u32			gsis[VIRTIO_PCI_MAX_VQ];
	u64			msix_pba;
	struct msix_table	msix_table[VIRTIO_PCI_MAX_VQ + VIRTIO_PCI_MAX_CONFIG];

	/* virtio queue */
	u16			queue_selector;
	struct virtio_pci_ioevent_param	ioeventfds[VIRTIO_PCI_MAX_VQ];
};

/* VIRTIO-BLK */

#define VIRTIO_BLK_MAX_DEV		4

/*
 * the header and status consume too entries
 */
#define DISK_SEG_MAX			(VIRTIO_BLK_QUEUE_SIZE - 2)
#define VIRTIO_BLK_QUEUE_SIZE		256
#define NUM_VIRT_QUEUES			1

struct blk_dev_req {
	struct virt_queue		*vq;
	struct blk_dev			*bdev;
	struct iovec			iov[VIRTIO_BLK_QUEUE_SIZE];
	u16				out, in, head;
	u8				*status;
	struct broiler			*broiler;
};

struct blk_dev {
	struct mutex			mutex;
	struct list_head		list;

	struct virtio_device		vdev;
	struct virtio_blk_config	blk_config;
	u64				capacity;
	struct disk_image		*disk;
	u32				features;

	struct virt_queue		vqs[NUM_VIRT_QUEUES];
	struct blk_dev_req		reqs[VIRTIO_BLK_QUEUE_SIZE];

	pthread_t			io_thread;
	int				io_efd;
	struct broiler			*broiler;
};

extern int virtio_blk_init(struct broiler *broiler);
extern int virtio_init(struct broiler *broiler, void *dev,
		struct virtio_device *vdev, struct virtio_ops *ops,
		enum virtio_trans trans, int device_id,
		int subsys_id, int calss);
extern int virtio_pci_signal_vq(struct broiler *broiler,
			struct virtio_device *vdev, u32 vq);
extern int virtio_pci_init(struct broiler *broiler, void *dev,
		struct virtio_device *vdev, int device_id, int, int);
extern int virtio_pci_exit(struct broiler *broiler, struct virtio_device *vdev);
extern int virtio_pci_reset(struct broiler *broiler, struct virtio_device *vdev);
extern int virtio_get_dev_specific_field(int offset, bool msix, u32 *config_off);
extern void virtio_set_guest_features(struct broiler *broiler,
		struct virtio_device *vdev, void *dev, u32 features);
extern void virtio_exit_vq(struct broiler *broiler, struct virtio_device *vdev,
                                void *dev, int num);
extern void virtio_notify_status(struct broiler *broiler,
                struct virtio_device *vdev, void *dev, u8 status);
extern struct vring_used_elem *virt_queue_set_used_elem(
			struct virt_queue *queue, u32 head, u32 len);
extern bool virtio_queue_should_signal(struct virt_queue *vq);
extern int virtio_compat_add_message(const char *device, const char *config);
extern u16 virt_queue_get_head_iov(struct virt_queue *vq, struct iovec iov[],
		u16 *out, u16 *in, u16 head, struct broiler *broiler);
extern int virtio_pci_signal_config(struct broiler *broiler,
                                        struct virtio_device *vdev);
extern bool virtio_access_config(struct broiler *broiler, struct virtio_device *vdev,
			void *dev, unsigned long offset, void *data,
			size_t size, bool is_write);

#endif
