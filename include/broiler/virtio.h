#ifndef _BISCUITOS_VIRTIO_H
#define _BISCUITOS_VIRTIO_H

#include <linux/virtio_blk.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include <pthread.h>
#include "linux/mutex.h"
#include "linux/list.h"
#include "broiler/disk.h"
#include "broiler/pci.h"
#include "broiler/device.h"
#include "broiler/utils.h"

#define VIRTIO_ENDIAN_LE	(1 << 0)
#define VIRTIO_ENDIAN_HOST	VIRTIO_ENDIAN_LE

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
	bool				use_vhost;
	void				*virtio;
	struct virtio_ops		*ops;
	u16				endian;
	u32				features;
	u32				status;
};

struct virtio_ops {
	u8 *(*get_config)(struct broiler *broiler, void *dev);
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

#endif
