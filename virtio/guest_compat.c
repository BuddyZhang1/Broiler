#include "broiler/broiler.h"
#include "broiler/compat.h"
#include "linux/mutex.h"

static LIST_HEAD(messages);
static DEFINE_MUTEX(compat_mtx);
static int id;

static void compat_free(struct compat_message *msg)
{
	free(msg->title);
	free(msg->desc);
	free(msg);
}

int compat_add_message(const char *title, const char *desc)
{
	struct compat_message *msg;
	int msg_id;

	msg = malloc(sizeof(*msg));
	if (msg == NULL)
		goto cleanup;

	msg->title = strdup(title);
	msg->desc = strdup(desc);

	if (msg->title == NULL || msg->desc == NULL)
		goto cleanup;

	mutex_lock(&compat_mtx);

	msg->id = msg_id = id++;
	list_add_tail(&msg->list, &messages);

	mutex_unlock(&compat_mtx);
	return msg_id;

cleanup:
	if (msg)
		compat_free(msg);

	return -ENOMEM;
}

int compat_remove_message(int id)
{
	struct compat_message *pos, *n;

	mutex_lock(&compat_mtx);

	list_for_each_entry_safe(pos, n, &messages, list) {
		if (pos->id == id) {
			list_del(&pos->list);
			compat_free(pos);

			mutex_unlock(&compat_mtx);
			return 0;
		}
	}

	mutex_unlock(&compat_mtx);

	return -ENOENT;
}
