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
