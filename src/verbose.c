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

#include "verbose.h"

#if !defined(VERBOSE_WITH_SYSLOG)

#include <stdio.h>
#include <stdarg.h>

int verbosity = 1;

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

void verbose(int level, const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", prefixes[level < 0 ? 0 : level > 7 ? 7 : level]);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, " [%s:%d]\n", file, line);
}

#endif

#if defined(VERBOSE_WITH_SYSLOG) && !defined(NDEBUG)

int verbosity = 1;

#endif

#if defined(VERBOSE_WITH_SYSLOG) && defined(NDEBUG)

void verbose_error(const char *file, int line)
{
	syslog(LOG_ERR, "error file %s line %d", file, line);
}

#endif


