/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
 * Author José Bollo <jose.bollo@iot.bzh>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Contain all generic part to handle REST/API
 * 
 *  https://www.gnu.org/software/libmicrohttpd/tutorial.html [search 'largepost.c']
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include <setjmp.h>

#include "afb-plugin.h"
#include "afb-req-itf.h"
#include "afb-poll-itf.h"

#include "session.h"
#include "afb-apis.h"
#include "verbose.h"
#include "utils-upoll.h"

struct api_desc {
	struct afb_api api;
	const char *name;
	size_t namelen;
};

static struct api_desc *apis_array = NULL;
static int apis_count = 0;

int afb_apis_count()
{
	return apis_count;
}

void afb_apis_free_context(int apiidx, void *context)
{
	const struct afb_api *api;
	api = &apis_array[apiidx].api;
	api->free_context(api->closure, context);
}

int afb_apis_add(const char *name, struct afb_api api)
{
	struct api_desc *apis;
	size_t len;
	int i;

	/* check existing or not */
	len = strlen(name);
	if (len == 0) {
		fprintf(stderr, "empty api name forbidden\n");
		goto error;
	}

	/* check previously existing plugin */
	for (i = 0 ; i < apis_count ; i++) {
		if (!strcasecmp(apis_array[i].name, name)) {
			fprintf(stderr, "ERROR: api of name %s already exists\n", name);
			goto error;
		}
	}

	/* allocates enough memory */
	apis = realloc(apis_array, ((unsigned)apis_count + 1) * sizeof * apis);
	if (apis == NULL) {
		fprintf(stderr, "out of memory\n");
		goto error;
	}
	apis_array = apis;

	/* record the plugin */
	apis = &apis_array[apis_count];
	apis->api = api;
	apis->namelen = len;
	apis->name = name;
	apis_count++;

	return 0;

error:
	return -1;
}

void afb_apis_call(struct afb_req req, struct AFB_clientCtx *context, const char *api, size_t lenapi, const char *verb, size_t lenverb)
{
	int i;
	const struct api_desc *a;

	a = apis_array;
	for (i = 0 ; i < apis_count ; i++, a++) {
		if (a->namelen == lenapi && !strncasecmp(a->name, api, lenapi)) {
			req.context = &context->contexts[i];
			a->api.call(a->api.closure, req, verb, lenverb);
			return;
		}
	}
	afb_req_fail(req, "fail", "api not found");
}

