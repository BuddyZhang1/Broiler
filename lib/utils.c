// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/utils.h"
#include <stdarg.h>

LIST_HEAD(broiler_dev_init_list);
LIST_HEAD(broiler_dev_exit_list);

int broiler_dev_init(struct broiler *broiler)
{
	struct init_entry *tmp;
	int r;

	list_for_each_entry(tmp, &broiler_dev_init_list, n) {
		r = tmp->func(broiler);
		if (r < 0)
			return r;
	}

	return r;
}

int broiler_dev_exit(struct broiler *broiler)
{
	struct init_entry *tmp;
	int r;

	list_for_each_entry(tmp, &broiler_dev_exit_list, n) {
		r = tmp->func(broiler);
		if (r < 0)
			return r;
	}

	return r;
}

static void report(const char *prefix, const char *err, va_list params)
{
	char msg[1024];

	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, " %s%s\n", prefix, msg);
}

static NORETURN void die_handler(const char *err, va_list params)
{
	report(" Fatal: ", err, params);
	exit(128);
}

void die(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	die_handler(err, params);
	va_end(params);
}

void die_perror(const char *s)
{
	perror(s);
	exit(1);
}
