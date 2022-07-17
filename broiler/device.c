// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/err.h"
#include "broiler/device.h"

static struct device_bus device_trees[DEVICE_BUS_MAX] = {
	[0 ... (DEVICE_BUS_MAX - 1)] = { RB_ROOT, 0 },
};

struct device *device_search(enum device_bus_type bus_type, u8 devnum)
{
	struct rb_node *node;

	if (bus_type >= DEVICE_BUS_MAX)
		return ERR_PTR(-EINVAL);

	node = device_trees[bus_type].root.rb_node;
	while (node) {
		struct device *dev = rb_entry(node, struct device, node);

		if (devnum < dev->dev_num)
			node = node->rb_left;
		else if (devnum > dev->dev_num)
			node = node->rb_right;
		else
			return dev;
	}

	return NULL;
}

struct device *device_first_dev(enum device_bus_type bus_type)
{
	struct rb_node *node;

	if (bus_type >= DEVICE_BUS_MAX)
		return NULL;

	node = rb_first(&device_trees[bus_type].root);
	return node ? rb_entry(node, struct device, node) : NULL;
}

struct device *device_next_dev(struct device *dev)
{
	struct rb_node *node = rb_next(&dev->node);
	return node ? rb_entry(node, struct device, node) : NULL;
}

struct device *device_find_dev(enum device_bus_type bus_type, u8 dev_num)
{
	struct rb_node *node;

	if (bus_type >= DEVICE_BUS_MAX)
		return ERR_PTR(-EINVAL);

	node = device_trees[bus_type].root.rb_node;
	while (node) {
		struct device *dev = rb_entry(node, struct device, node);

		if (dev_num < dev->dev_num)
			node = node->rb_left;
		else if (dev_num > dev->dev_num)
			node = node->rb_right;
		else
			return dev;
	}

	return NULL;
}

int device_register(struct device *dev)
{
	struct device_bus *bus;
	struct rb_node **node, *parent = NULL;

	if (dev->bus_type >= DEVICE_BUS_MAX) {
		printf("Ignoring device register on unknow bus %d\n",
					dev->bus_type);
		return -EINVAL;
	}

	bus = &device_trees[dev->bus_type];
	dev->dev_num = bus->dev_num++;

	node = &bus->root.rb_node;
	while (*node) {
		int num = rb_entry(*node, struct device, node)->dev_num;
		int result = dev->dev_num - num;

		parent = *node;
		if (result < 0)
			node = &((*node)->rb_left);
		else if (result > 0)
			node = &((*node)->rb_right);
		else
			return -EEXIST;
	}
	rb_link_node(&dev->node, parent, node);
	rb_insert_color(&dev->node, &bus->root);
	return 0;
}

void device_unregister(struct device *dev)
{
	struct device_bus *bus = &device_trees[dev->bus_type];
	rb_erase(&dev->node, &bus->root);
}
