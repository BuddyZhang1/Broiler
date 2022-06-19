#include "broiler/broiler.h"
#include "broiler/disk.h"
#include "broiler/err.h"
#include "broiler/utils.h"
#include <mntent.h>

static ssize_t raw_image_read(struct disk_image *disk, u64 sector,
		const struct iovec *iov, int iovcount, void *param)
{
	ssize_t count = get_iov_size(iov, iovcount);
	off_t offset = sector << SECTOR_SHIFT;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t nr;

		nr = broiler_pread(disk->fd, iov, iovcount, offset);
		if (nr <= 0) {
			if (total > 0)
				return total;

			return -1;
		}

		shift_iovec(&iov, &iovcount, nr, &total, &count, &offset);
	}
	return total;
}

static ssize_t raw_image_write(struct disk_image *disk, u64 sector,
		const struct iovec *iov, int iovcount, void *param)
{
	ssize_t count = get_iov_size(iov, iovcount);
	off_t offset = sector << SECTOR_SHIFT;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t nr;

		nr = broiler_pwrite(disk->fd, iov, iovcount, offset);
		if (nr < 0)
			return -1;
		if (nr == 0) {
			errno = ENOSPC;
			return -1;
		}

		shift_iovec(&iov, &iovcount, nr, &total, &count, &offset);
	}

	return total;
}

static struct disk_image *disk_image_new(int fd, u64 size,
			struct disk_image_operations *ops, int use_mmap)
{
	struct disk_image *disk;
	int r;

	disk = malloc(sizeof(*disk));
	if (!disk)
		return ERR_PTR(-ENOMEM);

	*disk = (struct disk_image) {
		.fd	= fd,
		.size	= size,
		.ops	= ops,
	};

	/* Don't support DISK_IMAGE_MMAP */
	return disk;
}

/* disk image operations */
static struct disk_image_operations raw_image_regular_ops = {
	.read	= raw_image_read,
	.write	= raw_image_write,
	.wait	= raw_image_wait,
	.async 	= true,
};

static struct disk_image *disk_image_open(const char *filename)
{
	struct disk_image *disk;
	struct stat st;
	int fd, flags;

	/* NO support readonly */
	flags = O_RDWR;
	if (stat(filename, &st) < 0)
		return ERR_PTR(-errno);

	fd = open(filename, flags);
	if (fd < 0)
		return ERR_PTR(fd);

	/* Only support RAW */
	disk = disk_image_new(fd, st.st_size, 
			&raw_image_regular_ops, DISK_IMAGE_REGULAR);
	if (!IS_ERR_OR_NULL(disk)) {
		disk->readonly = 0;
		return disk;
	}

	if (close(fd) < 0)
		printf("DISK CLOSE fialed\n");

	return ERR_PTR(-ENOSYS);
}

static int disk_image_close(struct disk_image *disk)
{
	if (!disk)
		return 0;

	if (disk->ops->close)
		disk->ops->close(disk);

	if (close(disk->fd) < 0)
		printf("CLOSE() failed.\n");

	free(disk);

	return 0;
}

int broiler_disk_image_init(struct broiler *broiler)
{
	struct disk_image **disks;
	const char *filename;
	int count, i;
	void *err;

	count = broiler->nr_disks;
	if (!count)
		return -EINVAL;
	if (count > MAX_DISK_IMAGES)
		return -ENOMEM;

	disks = calloc(count, sizeof(*disks));
	if (!disks)
		return -ENOMEM;
	
	for (i = 0; i < count; i++) {
		filename = broiler->disk_name[i];

		if (!filename)
			continue;

		disks[i] = disk_image_open(filename);
		if (IS_ERR_OR_NULL(disks[i])) {
			printf("Loading disk image %s failed.\n", filename);
			err = disks[i];
			goto error;
		}
	}

	broiler->disks = disks;
	return 0;

error:
	for (i = 0; i < count; i++)
		if (!IS_ERR_OR_NULL(disks[i]))
			disk_image_close(disks[i]);

	free(disks);
	return PTR_ERR(err);
}

int broiler_disk_image_exit(struct broiler *broiler)
{
	while (broiler->nr_disks)
		disk_image_close(broiler->disks[broiler->nr_disks--]);

	free(broiler->disks);

	return 0;
}