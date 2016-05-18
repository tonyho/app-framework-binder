/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
 * Author José Bollo <jose.bollo@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "session.h"
#include "verbose.h"
#include "afb-apis.h"
#include "afb-context.h"
#include <afb/afb-req-itf.h>

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

int afb_apis_is_valid_api_name(const char *name)
{
	unsigned char c;

	c = (unsigned char)*name;
	if (c == 0)
		return 0;
	do {
		if (c < (unsigned char)'\x80') {
			switch(c) {
			default:
				if (c > ' ')
					break;
			case '"':
			case '#':
			case '%':
			case '&':
			case '\'':
			case '/':
			case '?':
			case '`':
			case '\\':
			case '\x7f':
				return 0;
			}
		}
		c = (unsigned char)*++name;
	} while(c != 0);
	return 1;
}

int afb_apis_add(const char *name, struct afb_api api)
{
	struct api_desc *apis;
	int i;

	/* Checks the api name */
	if (!afb_apis_is_valid_api_name(name)) {
		ERROR("invalid api name forbidden (name is '%s')", name);
		goto error;
	}

	/* check previously existing plugin */
	for (i = 0 ; i < apis_count ; i++) {
		if (!strcasecmp(apis_array[i].name, name)) {
			ERROR("api of name %s already exists", name);
			goto error;
		}
	}

	/* allocates enough memory */
	apis = realloc(apis_array, ((unsigned)apis_count + 1) * sizeof * apis);
	if (apis == NULL) {
		ERROR("out of memory");
		goto error;
	}
	apis_array = apis;

	/* record the plugin */
	apis = &apis_array[apis_count];
	apis->api = api;
	apis->namelen = strlen(name);
	apis->name = name;
	apis_count++;

	return 0;

error:
	return -1;
}

void afb_apis_call_(struct afb_req req, struct afb_context *context, const char *api, const char *verb)
{
	afb_apis_call(req, context, api, strlen(api), verb, strlen(verb));
}

void afb_apis_call(struct afb_req req, struct afb_context *context, const char *api, size_t lenapi, const char *verb, size_t lenverb)
{
	int i;
	const struct api_desc *a;

	a = apis_array;
	for (i = 0 ; i < apis_count ; i++, a++) {
		if (a->namelen == lenapi && !strncasecmp(a->name, api, lenapi)) {
			context->api_index = i;
			a->api.call(a->api.closure, req, context, verb, lenverb);
			return;
		}
	}
	afb_req_fail(req, "fail", "api not found");
}

