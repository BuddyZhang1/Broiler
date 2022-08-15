// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/virtio.h"
#include "broiler/compat.h"

int virtio_get_dev_specific_field(int offset, bool msix, u32 *config_off)
{
	if (msix) {
		if (offset < 4)
			return VIRTIO_PCI_O_MSIX;
		else
			offset -= 4;
	}
	*config_off = offset;

	return VIRTIO_PCI_O_CONFIG;
}

void virtio_set_guest_features(struct broiler *broiler,
			struct virtio_device *vdev, void *dev, u32 features)
{
	vdev->features = features;
}

void virtio_exit_vq(struct broiler *broiler, struct virtio_device *vdev,
				void *dev, int num)
{
	struct virt_queue *vq = vdev->ops->get_vq(broiler, dev, num);

	if (vq->enabled && vdev->ops->exit_vq)
		vdev->ops->exit_vq(broiler, dev, num);
	memset(vq, 0, sizeof(*vq));
}

static struct vring_used_elem *
virt_queue_set_used_elem_no_update(struct virt_queue *queue, u32 head,
					u32 len, u16 offset)
{
	struct vring_used_elem *used_elem;
	u16 idx = virtio_guest_to_host_u16(queue, queue->vring.used->idx);

	idx += offset;
	used_elem	= &queue->vring.used->ring[idx % queue->vring.num];
	used_elem->id	= virtio_host_to_guest_u32(queue, head);
	used_elem->len	= virtio_host_to_guest_u32(queue, len);

	return used_elem;
}

static void
virt_queue_used_idx_advance(struct virt_queue *queue, u16 jump)
{
	u16 idx = virtio_guest_to_host_u16(queue, queue->vring.used->idx);

	/*
	 * Use wmb to assure that used elem was updated with head and len.
	 * We need a wmb here since we can't advance idx unless we're ready
	 * to pass the used element to the guest.
	 */
	wmb();
	idx += jump;
	queue->vring.used->idx = virtio_host_to_guest_u16(queue, idx);
}

struct vring_used_elem *virt_queue_set_used_elem(struct virt_queue *queue,
					u32 head, u32 len)
{
	struct vring_used_elem *used_elem;

	used_elem = virt_queue_set_used_elem_no_update(queue, head, len, 0);
	virt_queue_used_idx_advance(queue, 1);

	return used_elem;
}

bool virtio_queue_should_signal(struct virt_queue *vq)
{
	u16 old_idx, new_idx, event_idx;

	/*
	 * Use mb to assure used idx has been increased before we signal the
	 * guest, and we don't read a stale value for used_event. Without
	 * a mb here we might not send a notification that we need to send,
	 * or the guest may ignore the queue since it won't see an updated
	 * idx.
	 */
	mb();

	if (!vq->use_event_idx) {
		/*
		 * When VIRTIO_RING_F_EVENT_IDX isn't negotiated, interrupt
		 * the guest if it didn't explicitly request to be left
		 * alone.
		 */
		return !(virtio_guest_to_host_u16(vq, vq->vring.avail->flags) &
				VRING_AVAIL_F_NO_INTERRUPT);
	}

	old_idx		= vq->last_used_signalled;
	new_idx		= virtio_guest_to_host_u16(vq, vq->vring.used->idx);
	event_idx	= virtio_guest_to_host_u16(vq,
						vring_used_event(&vq->vring));

	if (vring_need_event(event_idx, new_idx, old_idx)) {
		vq->last_used_signalled = new_idx;
		return true;
	}
	return false;
}

void virtio_init_device_vq(struct broiler *broiler,
	struct virtio_device *vdev, struct virt_queue *vq, size_t nr_descs)
{
	struct vring_addr *addr = &vq->vring_addr;

	vq->endian		= vdev->endian;
	vq->use_event_idx	= (vdev->features & VIRTIO_RING_F_EVENT_IDX);
	vq->enabled		= true;

	if (addr->legacy) {
		unsigned long base = (u64)addr->pfn * addr->pgsize;
		void *p = gpa_flat_to_hva(broiler, base);

		vring_init(&vq->vring, nr_descs, p, addr->align);
	} else {
		u64 desc = (u64)addr->desc_hi << 32 | addr->desc_lo;
		u64 avail = (u64)addr->avail_hi << 32 | addr->avail_lo;
		u64 used = (u64)addr->used_hi << 32 | addr->used_lo;

		vq->vring = (struct vring) {
			.desc = gpa_flat_to_hva(broiler, desc),
			.used = gpa_flat_to_hva(broiler, used),
			.avail = gpa_flat_to_hva(broiler, avail),
			.num = nr_descs,
		};
	}
}

bool virtio_access_config(struct broiler *broiler, struct virtio_device *vdev,
			  void *dev, unsigned long offset, void *data,
			  size_t size, bool is_write)
{
	void *in, *out, *config;
	size_t config_size = vdev->ops->get_config_size(broiler, dev);

	if (offset + size > config_size) {
		printf("Config access offset (%lu) is beyond config size "
			"(%zu)\n", offset, config_size);
        	return false;
	}

	config = vdev->ops->get_config(broiler, dev) + offset;

	in = is_write ? data : config;
	out = is_write ? config : data;

	switch (size) {
	case 1:
		*(u8 *)out = *(u8 *)in;
		break;
	case 2:
		*(u16 *)out = *(u16 *)in;
		break;
	case 4:
		*(u32 *)out = *(u32 *)in;
		break;
	case 8:
		*(u64 *)out = *(u64 *)in;
		break;
	default:
		printf("%s: invalid access size\n", __func__);
		return false;
	}

	return true;
}

int virtio_compat_add_message(const char *device, const char *config)
{
	char *title, *desc;
	int len = 1024;
	int compat_id;

	title = malloc(len);
	if (!title)
		return -ENOMEM;

	desc = malloc(len);
	if (!desc) {
		free(title);
		return -ENOMEM;
	}

	snprintf(title, len, "%s device was not detected.", device);
	snprintf(desc, len, "While you have requested a %s device, "
			    "the guest kernel did not initialize it.\n"
			    "\tPlease make sure that the guest kernel was "
			    "compiled with %s=y enabled in .config.",
			    device, config);
	compat_id = compat_add_message(title, desc);

	free(desc);
	free(title);

	return compat_id;
}

void virtio_notify_status(struct broiler *broiler,
		struct virtio_device *vdev, void *dev, u8 status)
{
	u32 ext_status = status;

	vdev->status &= ~VIRTIO_CONFIG_S_MASK;
	vdev->status |= status;

	/* Add a few hints to help devices */
	if ((status & VIRTIO_CONFIG_S_DRIVER_OK) &&
			!(vdev->status & VIRTIO_STATUS_START)) {
		vdev->status |= VIRTIO_STATUS_START;
		ext_status |= VIRTIO_STATUS_START;
	} else if (!status && (vdev->status & VIRTIO_STATUS_START)) {
		vdev->status &= ~VIRTIO_STATUS_START;
		ext_status |= VIRTIO_STATUS_STOP;

		/*
		 * Reset virtqueues and stop all traffic now, so that the
		 * device can safely reset the backend in notify_status().
		 */
		vdev->ops->reset(broiler, vdev);
	}
	if (!status)
		ext_status |= VIRTIO_STATUS_CONFIG;

	if (vdev->ops->notify_status)
		vdev->ops->notify_status(broiler, dev, ext_status);
}

static inline bool virt_desc_test_flag(struct virt_queue *vq,
					struct vring_desc *desc, u16 flag)
{
	return !!(virtio_guest_to_host_u16(vq, desc->flags) & flag);
}

/*
 * Each buffer in the virtqueue is actually a chain of descriptors. This
 * function returns the next descriptor in the chain, or max if we're
 * at the end.
 */
static unsigned next_desc(struct virt_queue *vq, struct vring_desc *desc,
		unsigned int i, unsigned int max)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!virt_desc_test_flag(vq, &desc[i], VRING_DESC_F_NEXT))
		return max;

	next = virtio_guest_to_host_u16(vq, desc[i].next);

	/* Ensure they're not leading us off end of descriptors. */
	return min(next, max);
}

u16 virt_queue_get_head_iov(struct virt_queue *vq, struct iovec iov[],
			u16 *out, u16 *in, u16 head, struct broiler *broiler)
{
	struct vring_desc *desc;
	u16 idx, max;

	idx = head;
	*out = *in = 0;
	max = vq->vring.num;
	desc = vq->vring.desc;

	if (virt_desc_test_flag(vq, &desc[idx], VRING_DESC_F_INDIRECT)) {
		max = virtio_guest_to_host_u32(vq, desc[idx].len) /
					sizeof(struct vring_desc);
		desc = gpa_flat_to_hva(broiler,
			virtio_guest_to_host_u64(vq, desc[idx].addr));
		idx = 0;
	}

	do {
		/* Grab the first decriptor, and check it's OK. */
		iov[*out + *in].iov_len =
				virtio_guest_to_host_u32(vq, desc[idx].len);
		iov[*out + *in].iov_base =
				gpa_flat_to_hva(broiler,
				virtio_guest_to_host_u64(vq, desc[idx].addr));
		/* If this is an input descriptor, increment that count. */
		if (virt_desc_test_flag(vq, &desc[idx], VRING_DESC_F_WRITE))
			(*in)++;
		else
			(*out)++;
	} while ((idx = next_desc(vq, desc, idx, max)) != max);

	return head;
}

int virtio_init(struct broiler *broiler, void *dev,
		struct virtio_device *vdev, struct virtio_ops *ops,
		enum virtio_trans trans, int device_id,
		int subsys_id, int class)
{
	void *virtio;

	vdev->legacy		= true;
	virtio = calloc(sizeof(struct virtio_pci), 1);
	if (!virtio)
		return -ENOMEM;
	vdev->virtio		= virtio;
	vdev->ops		= ops;
	vdev->ops->signal_vq	= virtio_pci_signal_vq;
	vdev->ops->signal_config = virtio_pci_signal_config;
	vdev->ops->init		= virtio_pci_init;
	vdev->ops->exit		= virtio_pci_exit;
	vdev->ops->reset	= virtio_pci_reset;
	return vdev->ops->init(broiler, dev, 
			vdev, device_id, subsys_id, class);

}

int broiler_virtio_init(struct broiler *broiler)
{

	/* virtio-blk */
	virtio_blk_init(broiler);

	return 0;
}

int broiler_virtio_exit(struct broiler *broiler)
{
	return 0;
}
