// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/virtio.h"
#include "broiler/disk.h"
#include "broiler/compat.h"
#include "broiler/iovec.h"
#include <sys/eventfd.h>
#include <sys/prctl.h>

static LIST_HEAD(broiler_bdevs);
static int compat_id = -1;

static void virtio_blk_complete(void *param, long len)
{
	struct blk_dev_req *req = param;
	struct blk_dev *bdev = req->bdev;
	int queueid = req->vq - bdev->vqs;
	u8 *status;

	/* status */
	status = req->iov[req->out + req->in - 1].iov_base;
	*status = (len < 0) ? VIRTIO_BLK_S_IOERR : VIRTIO_BLK_S_OK;

	mutex_lock(&bdev->mutex);
	virt_queue_set_used_elem(req->vq, req->head, len);
	mutex_unlock(&bdev->mutex);

	if (virtio_queue_should_signal(&bdev->vqs[queueid]))
		bdev->vdev.ops->signal_vq(req->broiler, &bdev->vdev, queueid);
}

static u8 *get_config(struct broiler *broiler, void *dev)
{
	struct blk_dev *bdev = dev;

	return ((u8 *)(&bdev->blk_config));
}

static u32 get_host_features(struct broiler *broiler, void *dev)
{
	struct blk_dev *bdev = dev;

	return 1UL << VIRTIO_BLK_F_SEG_MAX
	       | 1UL << VIRTIO_BLK_F_FLUSH
	       | 1UL << VIRTIO_RING_F_EVENT_IDX
	       | 1UL << VIRTIO_RING_F_INDIRECT_DESC
	       | (bdev->disk->readonly ? 1UL << VIRTIO_BLK_F_RO : 0);
}

static void set_guest_features(struct broiler *broiler, void *dev, u32 features)
{
	struct blk_dev *bdev = dev;
	struct virtio_blk_config *conf = &bdev->blk_config;

	bdev->features = features;

	conf->capacity = virtio_host_to_guest_u64(&bdev->vdev, conf->capacity);
	conf->size_max = virtio_host_to_guest_u32(&bdev->vdev, conf->size_max);
	conf->seg_max = virtio_host_to_guest_u32(&bdev->vdev, conf->seg_max);

	/* Geometry */
	conf->geometry.cylinders = virtio_host_to_guest_u16(&bdev->vdev,
						conf->geometry.cylinders);
	conf->blk_size = virtio_host_to_guest_u32(&bdev->vdev, conf->blk_size);
	conf->min_io_size = virtio_host_to_guest_u16(&bdev->vdev,
						conf->min_io_size);
	conf->opt_io_size = virtio_host_to_guest_u32(&bdev->vdev,
						conf->opt_io_size);
}

static int get_vq_count(struct broiler *broiler, void *dev)
{
	return NUM_VIRT_QUEUES;
}

static void virtio_blk_do_io_request(struct broiler *broiler,
		struct virt_queue *vq, struct blk_dev_req *req)
{
	struct virtio_blk_outhdr req_hdr;
	size_t iovcount, last_iov;
	struct blk_dev *bdev;
	struct iovec *iov;
	ssize_t len;
	u32 type;
	u64 sector;

	bdev = req->bdev;
	iov  = req->iov;

	iovcount = req->out;
	len = memcpy_fromiovec_safe(&req_hdr,
				&iov, sizeof(req_hdr), &iovcount);
	if (len) {
		printf("Failed to get header\n");
		return;
	}

	type = virtio_guest_to_host_u32(vq, req_hdr.type);
	sector = virtio_guest_to_host_u64(vq, req_hdr.type);

	iovcount += req->in;
	if (!iov_size(iov, iovcount)) {
		printf("Invalid IOV\n");
		return;
	}

	/* Extract status byte from iovec */
	last_iov = iovcount - 1;
	while (!iov[last_iov].iov_len)
		last_iov--;
	iov[last_iov].iov_len--;
	req->status = iov[last_iov].iov_base + iov[last_iov].iov_len;
	if (!iov[last_iov].iov_len)
		iovcount--;

	switch (type) {
	case VIRTIO_BLK_T_IN:
		disk_image_read(bdev->disk, sector, iov, iovcount, req);
		break;
	case VIRTIO_BLK_T_OUT:
		disk_image_write(bdev->disk, sector, iov, iovcount, req);
		break;
	case VIRTIO_BLK_T_FLUSH:
		len = disk_image_flush(bdev->disk);
		virtio_blk_complete(req, len);
		break;
	case VIRTIO_BLK_T_GET_ID:
		len = disk_image_get_serial(bdev->disk, iov, iovcount,
						VIRTIO_BLK_ID_BYTES);
		virtio_blk_complete(req, len);
		break;
	default:
		printf("request type %d\n", type);
		break;
	}
}

static void virtio_blk_do_io(struct broiler *broiler,
			struct virt_queue *vq, struct blk_dev *bdev)
{
	struct blk_dev_req *req;
	u16 head;

	while (virt_queue_available(vq)) {
		head		= virt_queue_pop(vq);
		req		= &bdev->reqs[head];
		req->head	= virt_queue_get_head_iov(vq, req->iov,
					&req->out, &req->in, head, broiler);
		req->vq		= vq;

		virtio_blk_do_io_request(broiler, vq, req);
	}
}

static void *virtio_blk_thread(void *dev)
{
	struct blk_dev *bdev = dev;
	u64 data;
	int r;

	prctl(PR_SET_NAME, "virtio-blk-io");

	while (1) {
		r = read(bdev->io_efd, &data, sizeof(u64));
		if (r < 0)
			continue;
		virtio_blk_do_io(bdev->broiler, &bdev->vqs[0], bdev);
	}

	pthread_exit(NULL);
	return NULL;
}

static int init_vq(struct broiler *broiler, void *dev, u32 vq,
					u32 page_size, u32 align, u32 pfn)
{
	struct blk_dev *bdev = dev;
	struct virt_queue *queue;
	unsigned int i;
	void *p;

	compat_remove_message(compat_id);

	queue		= &bdev->vqs[vq];
	queue->pfn	= pfn;
	p		= virtio_get_vq(broiler, queue->pfn, page_size);

	vring_init(&queue->vring, VIRTIO_BLK_QUEUE_SIZE, p, align);
	virtio_init_device_vq(&bdev->vdev, queue);

	if (vq != 0)
		return 0;

	for (i = 0; i < ARRAY_SIZE(bdev->reqs); i++) {
		bdev->reqs[i] = (struct blk_dev_req) {
			.bdev =bdev,
			.broiler = broiler,
		};
	}

	mutex_init(&bdev->mutex);
	bdev->io_efd = eventfd(0, 0);
	if (bdev->io_efd < 0)
		return -errno;

	if (pthread_create(&bdev->io_thread, NULL, virtio_blk_thread, bdev))
		return -errno;

	return 0;
}

static void exit_vq(struct broiler *broiler, void *dev, u32 vq)
{
	struct blk_dev *bdev = dev;

	if (vq != 0)
		return;

	close(bdev->io_efd);
	pthread_cancel(bdev->io_thread);
	pthread_join(bdev->io_thread, NULL);

	disk_image_wait(bdev->disk);
}

static void notify_status(struct broiler *broiler, void *dev, u32 status)
{
	struct blk_dev *bdev = dev;
	struct virtio_blk_config *conf = &bdev->blk_config;

	if (!(status & VIRTIO_STATUS_CONFIG))
		return;

	conf->capacity = virtio_host_to_guest_u64(&bdev->vdev, bdev->capacity);
	conf->seg_max = virtio_host_to_guest_u32(&bdev->vdev, DISK_SEG_MAX);
}

static int notify_vq(struct broiler *broiler, void *dev, u32 vq)
{
	struct blk_dev *bdev = dev;
	u64 data = 1;
	int r;

	r = write(bdev->io_efd, &data, sizeof(data));
	if (r < 0)
		return r;

	return 0;
}

static struct virt_queue *get_vq(struct broiler *broiler, void *dev, u32 vq)
{
	struct blk_dev *bdev = dev;

	return &bdev->vqs[vq];
}

static int get_size_vq(struct broiler *broiler, void *dev, u32 vq)
{
	return VIRTIO_BLK_QUEUE_SIZE;
}

static int set_size_vq(struct broiler *broiler, void *dev, u32 vq, int size)
{
	return size;
}

static struct virtio_ops virtio_blk_ops = {
	.get_config			= get_config,
	.get_host_features		= get_host_features,
	.set_guest_features		= set_guest_features,
	.get_vq_count			= get_vq_count,
	.init_vq			= init_vq,
	.exit_vq			= exit_vq,
	.notify_status			= notify_status,
	.notify_vq			= notify_vq,
	.get_vq				= get_vq,
	.get_size_vq			= get_size_vq,
	.set_size_vq			= set_size_vq,
};

static int virtio_blk_init_one(struct broiler *broiler, struct disk_image *disk)
{
	struct blk_dev *bdev;
	int r;

	if (!disk)
		return -EINVAL;

	bdev = calloc(1, sizeof(struct blk_dev));
	if (bdev == NULL)
		return -ENOMEM;

	*bdev = (struct blk_dev) {
		.disk			= disk,
		.capacity		= disk->size / SECTOR_SIZE,
		.broiler		= broiler,
	};
	list_add_tail(&bdev->list, &broiler_bdevs);

	r = virtio_init(broiler, bdev, &bdev->vdev,
			&virtio_blk_ops,
			VIRTIO_DEFAULT_TRANS(broiler),
			PCI_DEVICE_ID_VIRTIO_BLK,
			VIRTIO_ID_BLOCK, PCI_CLASS_BLK);
	if (r < 0)
		return r;

	disk_image_set_callback(bdev->disk, virtio_blk_complete);
	if (compat_id == -1)
		compat_id = virtio_compat_add_message("virtio-blk",
						"CONFIG_VIRTIO_BLK");
	return 0;
}

static int virtio_blk_exit_one(struct broiler *broiler, struct blk_dev *bdev)
{
	list_del(&bdev->list);
	free(bdev);

	return 0;
}

int virtio_blk_exit(struct broiler *broiler)
{
	while (!list_empty(&broiler_bdevs)) {
		struct blk_dev *bdev;

		bdev = list_first_entry(&broiler_bdevs, struct blk_dev, list);
		virtio_blk_exit_one(broiler, bdev);
	}

	return 0;
}

int virtio_blk_init(struct broiler *broiler)
{
	int i, r = 0;

	for (i = 0; i < broiler->nr_disks; i++) {
		r = virtio_blk_init_one(broiler, broiler->disks[i]);
		if (r < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	virtio_blk_exit(broiler);
	return r;
}
