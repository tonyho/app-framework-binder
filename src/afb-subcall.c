/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author: José Bollo <jose.bollo@iot.bzh>
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
#include <string.h>

#include <json-c/json.h>
#include <afb/afb-req-itf.h>

#include "afb-subcall.h"
#include "afb-msg-json.h"
#include "afb-apis.h"
#include "afb-context.h"
#include "verbose.h"

struct afb_subcall;

static void subcall_addref(struct afb_subcall *subcall);
static void subcall_unref(struct afb_subcall *subcall);
static struct json_object *subcall_json(struct afb_subcall *subcall);
static struct afb_arg subcall_get(struct afb_subcall *subcall, const char *name);
static void subcall_fail(struct afb_subcall *subcall, const char *status, const char *info);
static void subcall_success(struct afb_subcall *subcall, struct json_object *obj, const char *info);
static const char *subcall_raw(struct afb_subcall *subcall, size_t *size);
static void subcall_send(struct afb_subcall *subcall, const char *buffer, size_t size);
static int subcall_subscribe(struct afb_subcall *subcall, struct afb_event event);
static int subcall_unsubscribe(struct afb_subcall *subcall, struct afb_event event);
static void subcall_session_close(struct afb_subcall *subcall);
static int subcall_session_set_LOA(struct afb_subcall *subcall, unsigned loa);
static void subcall_subcall(struct afb_subcall *subcall, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure);

const struct afb_req_itf afb_subcall_req_itf = {
	.json = (void*)subcall_json,
	.get = (void*)subcall_get,
	.success = (void*)subcall_success,
	.fail = (void*)subcall_fail,
	.raw = (void*)subcall_raw,
	.send = (void*)subcall_send,
	.context_get = (void*)afb_context_get,
	.context_set = (void*)afb_context_set,
	.addref = (void*)subcall_addref,
	.unref = (void*)subcall_unref,
	.session_close = (void*)subcall_session_close,
	.session_set_LOA = (void*)subcall_session_set_LOA,
	.subscribe = (void*)subcall_subscribe,
	.unsubscribe = (void*)subcall_unsubscribe,
	.subcall = (void*)subcall_subcall
};

struct afb_subcall
{
	/*
	 * CAUTION: 'context' field should be the first because there
	 * is an implicit convertion to struct afb_context
	 */
	struct afb_context context;
	struct afb_context *original_context;
	int refcount;
	struct json_object *args;
	struct afb_req req;
	void (*callback)(void*, int, struct json_object*);
	void *closure;
};

static void subcall_addref(struct afb_subcall *subcall)
{
	subcall->refcount++;
}

static void subcall_unref(struct afb_subcall *subcall)
{
	if (0 == --subcall->refcount) {
		json_object_put(subcall->args);
		afb_req_unref(subcall->req);
		free(subcall);
	}
}

static struct json_object *subcall_json(struct afb_subcall *subcall)
{
	return subcall->args;
}

static struct afb_arg subcall_get(struct afb_subcall *subcall, const char *name)
{
	return afb_msg_json_get_arg(subcall->args, name);
}

static void subcall_emit(struct afb_subcall *subcall, int iserror, struct json_object *object)
{
	if (subcall->context.refreshing != 0)
		subcall->original_context->refreshing = 1;

	subcall->callback(subcall->closure, iserror, object);
	json_object_put(object);
}

static void subcall_fail(struct afb_subcall *subcall, const char *status, const char *info)
{
	subcall_emit(subcall, 1, afb_msg_json_reply_error(status, info, NULL, NULL));
}

static void subcall_success(struct afb_subcall *subcall, struct json_object *obj, const char *info)
{
	subcall_emit(subcall, 0, afb_msg_json_reply_ok(info, obj, NULL, NULL));
}

static const char *subcall_raw(struct afb_subcall *subcall, size_t *size)
{
	const char *result = json_object_to_json_string(subcall->args);
	if (size != NULL)
		*size = strlen(result);
	return result;
}

static void subcall_send(struct afb_subcall *subcall, const char *buffer, size_t size)
{
	subcall_emit(subcall, 0, json_tokener_parse(buffer));
}

static void subcall_session_close(struct afb_subcall *subcall)
{
	afb_req_session_close(subcall->req);
}

static int subcall_session_set_LOA(struct afb_subcall *subcall, unsigned loa)
{
	return afb_req_session_set_LOA(subcall->req, loa);
}

static int subcall_subscribe(struct afb_subcall *subcall, struct afb_event event)
{
	return afb_req_subscribe(subcall->req, event);
}

static int subcall_unsubscribe(struct afb_subcall *subcall, struct afb_event event)
{
	return afb_req_unsubscribe(subcall->req, event);
}

static void subcall_subcall(struct afb_subcall *subcall, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure)
{
	afb_subcall(&subcall->context, api, verb, args, callback, closure, (struct afb_req){ .itf = &afb_subcall_req_itf, .closure = subcall });
}

void afb_subcall(struct afb_context *context, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure, struct afb_req req)
{
	struct afb_subcall *subcall;

	subcall = calloc(1, sizeof *subcall);
	if (subcall == NULL) {
		callback(closure, 1, afb_msg_json_reply_error("failed", "out of memory", NULL, NULL));
		return;
	}

	subcall->original_context = context;
	subcall->refcount = 1;
	subcall->args = args;
	subcall->req = req;
	subcall->callback = callback;
	subcall->closure = closure;
	subcall->context = *context;
	afb_req_addref(req);
	afb_apis_call_((struct afb_req){ .itf = &afb_subcall_req_itf, .closure = subcall }, &subcall->context, api, verb);
	subcall_unref(subcall);
}


