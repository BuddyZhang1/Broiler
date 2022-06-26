#include "broiler/broiler.h"
#include "broiler/utils.h"
#include <stdarg.h>

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
