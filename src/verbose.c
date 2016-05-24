/*
 Copyright (C) 2016 "IoT.bzh"

 author: José Bollo <jose.bollo@iot.bzh>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#include <stdio.h>
#include <stdarg.h>

#include "verbose.h"

int verbosity = 1;

#define LEVEL(x) ((x) < 0 ? 0 : (x) > 7 ? 7 : (x))

#if defined(VERBOSE_WITH_SYSLOG)

#include <syslog.h>

void vverbose(int level, const char *file, int line, const char *fmt, va_list args)
{
	char *p;

	if (file == NULL || vasprintf(&p, fmt, args) < 0)
		vsyslog(level, fmt, args);
	else {
		syslog(LEVEL(level), "%s [%s:%d]", p, file, line);
		free(p);
	}
}

void verbose_set_name(const char *name, int authority)
{
	openlog(name, LOG_PERROR, authority ? LOG_AUTH : LOG_USER);
}

#else

static const char *prefixes[] = {
	"<0> EMERGENCY",
	"<1> ALERT",
	"<2> CRITICAL",
	"<3> ERROR",
	"<4> WARNING",
	"<5> NOTICE",
	"<6> INFO",
	"<7> DEBUG"
};

void vverbose(int level, const char *file, int line, const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", prefixes[LEVEL(level)]);
	vfprintf(stderr, fmt, args);
	if (file != NULL)
		fprintf(stderr, " [%s:%d]\n", file, line);
	else
		fprintf(stderr, "\n");
}

void verbose_set_name(const char *name, int authority)
{
	fprintf(stderr, "%s: application name is '%s' for '%s'\n", prefixes[5], name, authority ? "AUTHORITY" : "USER");
}

#endif

void verbose(int level, const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vverbose(level, file, line, fmt, ap);
	va_end(ap);
}

