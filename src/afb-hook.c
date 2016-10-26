/*
 * Copyright (C) 2016 "IoT.bzh"
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <json-c/json.h>

#include <afb/afb-req-itf.h>
#include <afb/afb-event-itf.h>

#include "afb-context.h"
#include "afb-hook.h"
#include "session.h"
#include "verbose.h"

/*
 * Trace 
 */
struct afb_hook {
	struct afb_hook *next; /* next hook */
	unsigned refcount; /* reference count */
	char *api; 
	char *verb;
	struct AFB_clientCtx *session;
	unsigned flags; /* hook flags */
	struct afb_hook_req_itf *reqitf;
	void *closure;
};

struct hook_req_observer {
	struct afb_hook *hook;
	struct hook_req_observer *next;
};

/*
 * Structure recording a request to hook
 */
struct afb_hook_req {
	struct hook_req_observer *observers; /* observers */
	struct afb_context *context; /* context of the request */
	struct afb_req req; /* the request hookd */
	unsigned refcount; /* reference count proxy for request */
	char name[1]; /* hook info for the request */
};

/*
 * Structure for handling subcalls callbacks
 */
struct hook_subcall {
	struct afb_hook_req *tr; /* hookd request */
	void (*callback)(void*, int, struct json_object*); /* client callback */
	void *cb_closure; /* cient closure */
};

static unsigned hook_count = 0;

static struct afb_hook *list_of_hooks = NULL;

/******************************************************************************
 * section: default callbacks for tracing requests
 *****************************************************************************/

static void _hook_(const struct afb_hook_req *tr, const char *format, ...)
{
	int len;
	char *buffer;
	va_list ap;

	va_start(ap, format);
	len = vasprintf(&buffer, format, ap);
	va_end(ap);

	if (len < 0)
		NOTICE("tracing %s allocation error", tr->name);
	else {
		NOTICE("hook %s %s", tr->name, buffer);
		free(buffer);
	}
}

static void hook_req_begin_default_cb(void * closure, const struct afb_hook_req *tr)
{
	_hook_(tr, "BEGIN");
}

static void hook_req_end_default_cb(void * closure, const struct afb_hook_req *tr)
{
	_hook_(tr, "END");
}

static void hook_req_json_default_cb(void * closure, const struct afb_hook_req *tr, struct json_object *obj)
{
	_hook_(tr, "json() -> %s", json_object_to_json_string(obj));
}

static void hook_req_get_default_cb(void * closure, const struct afb_hook_req *tr, const char *name, struct afb_arg arg)
{
	_hook_(tr, "get(%s) -> { name: %s, value: %s, path: %s }", name, arg.name, arg.value, arg.path);
}

static void hook_req_success_default_cb(void * closure, const struct afb_hook_req *tr, struct json_object *obj, const char *info)
{
	_hook_(tr, "success(%s, %s)", json_object_to_json_string(obj), info);
}

static void hook_req_fail_default_cb(void * closure, const struct afb_hook_req *tr, const char *status, const char *info)
{
	_hook_(tr, "fail(%s, %s)", status, info);
}

static void hook_req_raw_default_cb(void * closure, const struct afb_hook_req *tr, const char *buffer, size_t size)
{
	_hook_(tr, "raw() -> %.*s", (int)size, buffer);
}

static void hook_req_send_default_cb(void * closure, const struct afb_hook_req *tr, const char *buffer, size_t size)
{
	_hook_(tr, "send(%.*s)", (int)size, buffer);
}

static void hook_req_context_get_default_cb(void * closure, const struct afb_hook_req *tr, void *value)
{
	_hook_(tr, "context_get() -> %p", value);
}

static void hook_req_context_set_default_cb(void * closure, const struct afb_hook_req *tr, void *value, void (*free_value)(void*))
{
	_hook_(tr, "context_set(%p, %p)", value, free_value);
}

static void hook_req_addref_default_cb(void * closure, const struct afb_hook_req *tr)
{
	_hook_(tr, "addref()");
}

static void hook_req_unref_default_cb(void * closure, const struct afb_hook_req *tr)
{
	_hook_(tr, "unref()");
}

static void hook_req_session_close_default_cb(void * closure, const struct afb_hook_req *tr)
{
	_hook_(tr, "session_close()");
}

static void hook_req_session_set_LOA_default_cb(void * closure, const struct afb_hook_req *tr, unsigned level, int result)
{
	_hook_(tr, "session_set_LOA(%u) -> %d", level, result);
}

static void hook_req_subscribe_default_cb(void * closure, const struct afb_hook_req *tr, struct afb_event event, int result)
{
	_hook_(tr, "subscribe(%s:%p) -> %d", afb_event_name(event), event.closure, result);
}

static void hook_req_unsubscribe_default_cb(void * closure, const struct afb_hook_req *tr, struct afb_event event, int result)
{
	_hook_(tr, "unsubscribe(%s:%p) -> %d", afb_event_name(event), event.closure, result);
}

static void hook_req_subcall_default_cb(void * closure, const struct afb_hook_req *tr, const char *api, const char *verb, struct json_object *args)
{
	_hook_(tr, "subcall(%s/%s, %s) ...", api, verb, json_object_to_json_string(args));
}

static void hook_req_subcall_result_default_cb(void * closure, const struct afb_hook_req *tr, int status, struct json_object *result)
{
	_hook_(tr, "    ...subcall... -> %d: %s", status, json_object_to_json_string(result));
}

static struct afb_hook_req_itf hook_req_default_itf = {
	.hook_req_begin = hook_req_begin_default_cb,
	.hook_req_end = hook_req_end_default_cb,
	.hook_req_json = hook_req_json_default_cb,
	.hook_req_get = hook_req_get_default_cb,
	.hook_req_success = hook_req_success_default_cb,
	.hook_req_fail = hook_req_fail_default_cb,
	.hook_req_raw = hook_req_raw_default_cb,
	.hook_req_send = hook_req_send_default_cb,
	.hook_req_context_get = hook_req_context_get_default_cb,
	.hook_req_context_set = hook_req_context_set_default_cb,
	.hook_req_addref = hook_req_addref_default_cb,
	.hook_req_unref = hook_req_unref_default_cb,
	.hook_req_session_close = hook_req_session_close_default_cb,
	.hook_req_session_set_LOA = hook_req_session_set_LOA_default_cb,
	.hook_req_subscribe = hook_req_subscribe_default_cb,
	.hook_req_unsubscribe = hook_req_unsubscribe_default_cb,
	.hook_req_subcall = hook_req_subcall_default_cb,
	.hook_req_subcall_result = hook_req_subcall_result_default_cb,
};

/******************************************************************************
 * section: macro for tracing requests
 *****************************************************************************/

#define TRACE_REQX(what,tr) do{\
		struct hook_req_observer *observer = tr->observers;\
		while (observer != NULL) {\
			struct afb_hook *hook = observer->hook;\
			observer = observer->next;\
			if (hook->reqitf && hook->reqitf->hook_req_##what)\
				hook->reqitf->hook_req_##what(hook->closure, tr);\
		}\
	}while(0)

#define TRACE_REQ_(what,tr) do{\
		struct hook_req_observer *observer = tr->observers;\
		while (observer != NULL) {\
			struct afb_hook *hook = observer->hook;\
			observer = observer->next;\
			if ((hook->flags & afb_hook_flag_req_##what) && hook->reqitf && hook->reqitf->hook_req_##what)\
				hook->reqitf->hook_req_##what(hook->closure, tr);\
		}\
	}while(0)

#define TRACE_REQ(what,tr,...) do{\
		struct hook_req_observer *observer = tr->observers;\
		while (observer != NULL) {\
			struct afb_hook *hook = observer->hook;\
			observer = observer->next;\
			if ((hook->flags & afb_hook_flag_req_##what) && hook->reqitf && hook->reqitf->hook_req_##what)\
				hook->reqitf->hook_req_##what(hook->closure, tr, __VA_ARGS__);\
		}\
	}while(0)

/******************************************************************************
 * section: afb_hook_req handling
 *****************************************************************************/

static void hook_req_addref(struct afb_hook_req *tr)
{
	tr->refcount++;
}

static void hook_req_unref(struct afb_hook_req *tr)
{
	struct hook_req_observer *o1, *o2;
	if (!--tr->refcount) {
		TRACE_REQX(end, tr);
		afb_req_unref(tr->req);
		o1 = tr->observers;
		while(o1) {
			afb_hook_unref(o1->hook);
			o2 = o1->next;
			free(o1);
			o1 = o2;
		}
		free(tr);
	}
}

static struct afb_hook_req *hook_req_create(struct afb_req req, struct afb_context *context, const char *api, size_t lenapi, const char *verb, size_t lenverb)
{
	unsigned id;
	struct afb_hook_req *tr;

	tr = malloc(sizeof *tr + 8 + lenapi + lenverb);
	if (tr != NULL) {
		/* get the call id */
		id = ++hook_count;
		if (id == 1000000)
			id = hook_count = 1;

		/* init hook */
		tr->observers = NULL;
		tr->refcount = 1;
		tr->context = context;
		tr->req = req;
		afb_req_addref(req);
		snprintf(tr->name, 9 + lenapi + lenverb, "%06d:%.*s/%.*s", id, (int)lenapi, api, (int)lenverb, verb);
	}
	return tr;
}

static void hook_req_add_observer(struct afb_hook_req *tr, struct afb_hook *hook)
{
	struct hook_req_observer *observer;

	observer = malloc(sizeof *observer);
	if (observer) {
		observer->hook = afb_hook_addref(hook);
		observer->next = tr->observers;
		tr->observers = observer;
	}
}

/******************************************************************************
 * section: hooks for tracing requests
 *****************************************************************************/

static struct json_object *req_hook_json(void *closure)
{
	struct afb_hook_req *tr = closure;
	struct json_object *r;

	r = afb_req_json(tr->req);
	TRACE_REQ(json, tr, r);
	return r;
}

static struct afb_arg req_hook_get(void *closure, const char *name)
{
	struct afb_hook_req *tr = closure;
	struct afb_arg a;

	a = afb_req_get(tr->req, name);
	TRACE_REQ(get, tr, name, a);
	return a;
}

static void req_hook_success(void *closure, struct json_object *obj, const char *info)
{
	struct afb_hook_req *tr = closure;

	TRACE_REQ(success, tr, obj, info);
	afb_req_success(tr->req, obj, info);
	hook_req_unref(tr);
}

static void req_hook_fail(void *closure, const char *status, const char *info)
{
	struct afb_hook_req *tr = closure;

	TRACE_REQ(fail, tr, status, info);
	afb_req_fail(tr->req, status, info);
	hook_req_unref(tr);
}

static const char *req_hook_raw(void *closure, size_t *size)
{
	struct afb_hook_req *tr = closure;
	const char *r;
	size_t s;

	r = afb_req_raw(tr->req, &s);
	TRACE_REQ(raw, tr, r, s);
	if (size)
		*size = s;
	return r;
}

static void req_hook_send(void *closure, const char *buffer, size_t size)
{
	struct afb_hook_req *tr = closure;

	TRACE_REQ(send, tr, buffer, size);
	afb_req_send(tr->req, buffer, size);
}

static void *req_hook_context_get(void *closure)
{
	struct afb_hook_req *tr = closure;
	void *r;

	r = afb_req_context_get(tr->req);
	TRACE_REQ(context_get, tr, r);

	return r;
}

static void req_hook_context_set(void *closure, void *value, void (*free_value)(void*))
{
	struct afb_hook_req *tr = closure;

	TRACE_REQ(context_set, tr, value, free_value);
	afb_req_context_set(tr->req, value, free_value);
}

static void req_hook_addref(void *closure)
{
	struct afb_hook_req *tr = closure;

	TRACE_REQ_(addref, tr);
	hook_req_addref(tr);
}

static void req_hook_unref(void *closure)
{
	struct afb_hook_req *tr = closure;

	TRACE_REQ_(unref, tr);
	hook_req_unref(tr);
}

static void req_hook_session_close(void *closure)
{
	struct afb_hook_req *tr = closure;

	TRACE_REQ_(session_close, tr);
	afb_req_session_close(tr->req);
}

static int req_hook_session_set_LOA(void *closure, unsigned level)
{
	struct afb_hook_req *tr = closure;
	int r;

	r = afb_req_session_set_LOA(tr->req, level);
	TRACE_REQ(session_set_LOA, tr, level, r);
	return r;
}

static int req_hook_subscribe(void *closure, struct afb_event event)
{
	struct afb_hook_req *tr = closure;
	int r;

	r = afb_req_subscribe(tr->req, event);
	TRACE_REQ(subscribe, tr, event, r);
	return r;
}

static int req_hook_unsubscribe(void *closure, struct afb_event event)
{
	struct afb_hook_req *tr = closure;
	int r;

	r = afb_req_unsubscribe(tr->req, event);
	TRACE_REQ(unsubscribe, tr, event, r);
	return r;
}

static void req_hook_subcall_result(void *closure, int status, struct json_object *result)
{
	struct hook_subcall *sc = closure;
	struct hook_subcall s = *sc;

	free(sc);
	TRACE_REQ(subcall_result, s.tr, status, result);
	hook_req_unref(s.tr);
	s.callback(s.cb_closure, status, result);
}

static void req_hook_subcall(void *closure, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *cb_closure)
{
	struct afb_hook_req *tr = closure;
	struct hook_subcall *sc;

	TRACE_REQ(subcall, tr, api, verb, args);
	sc = malloc(sizeof *sc);
	if (sc) {
		sc->tr = tr;
		sc->callback = callback;
		sc->cb_closure = cb_closure;
		hook_req_addref(tr);
		cb_closure = sc;
		callback = req_hook_subcall_result;
	}
	afb_req_subcall(tr->req, api, verb, args, callback, cb_closure);
}

static struct afb_req_itf req_hook_itf = {
	.json = req_hook_json,
	.get = req_hook_get,
	.success = req_hook_success,
	.fail = req_hook_fail,
	.raw = req_hook_raw,
	.send = req_hook_send,
	.context_get = req_hook_context_get,
	.context_set = req_hook_context_set,
	.addref = req_hook_addref,
	.unref = req_hook_unref,
	.session_close = req_hook_session_close,
	.session_set_LOA = req_hook_session_set_LOA,
	.subscribe = req_hook_subscribe,
	.unsubscribe = req_hook_unsubscribe,
	.subcall = req_hook_subcall
};

/******************************************************************************
 * section: 
 *****************************************************************************/

struct afb_req afb_hook_req_call(struct afb_req req, struct afb_context *context, const char *api, size_t lenapi, const char *verb, size_t lenverb)
{
	int add;
	struct afb_hook_req *tr;
	struct afb_hook *hook;

	hook = list_of_hooks;
	if (hook) {
		tr = NULL;
		do {
			add = (hook->flags & afb_hook_flags_req_all) != 0
			   && (!hook->session || hook->session == context->session)
			   && (!hook->api || !(memcmp(hook->api, api, lenapi) || hook->api[lenapi]))
			   && (!hook->verb || !(memcmp(hook->verb, verb, lenverb) || hook->verb[lenverb]));
			if (add) {
				if (!tr)
					tr = hook_req_create(req, context, api, lenapi, verb, lenverb);
				if (tr)
					hook_req_add_observer(tr, hook);
			}
			hook = hook->next;
		} while(hook);
		if (tr) {
			req.closure = tr;
			req.itf = &req_hook_itf;
			TRACE_REQX(begin, tr);
		}
	}

	return req;
}

struct afb_hook *afb_hook_req_create(const char *api, const char *verb, struct AFB_clientCtx *session, unsigned flags, struct afb_hook_req_itf *itf, void *closure)
{
	struct afb_hook *hook;

	hook = malloc(sizeof *hook);
	if (hook == NULL)
		return NULL;

	hook->api = api ? strdup(api) : NULL;
	hook->verb = verb ? strdup(verb) : NULL;
	hook->session = session ? ctxClientAddRef(session) : NULL;

	if ((api && !hook->api) || (verb && !hook->verb)) {
		free(hook->api);
		free(hook->verb);
		if (hook->session)
			ctxClientUnref(hook->session);
		free(hook);
		return NULL;
	}

	hook->refcount = 1;
	hook->flags = flags;
	hook->reqitf = itf ? itf : &hook_req_default_itf;
	hook->closure = closure;

	hook->next = list_of_hooks;
	list_of_hooks = hook;
	return hook;
}

struct afb_hook *afb_hook_addref(struct afb_hook *hook)
{
	hook->refcount++;
	return hook;
}

void afb_hook_unref(struct afb_hook *hook)
{
	if (!--hook->refcount) {
		/* unlink */
		struct afb_hook **prv = &list_of_hooks;
		while (*prv && *prv != hook)
			prv = &(*prv)->next;
		if(*prv)
			*prv = hook->next;

		/* free */
		free(hook->api);
		free(hook->verb);
		if (hook->session)
			ctxClientUnref(hook->session);
		free(hook);
	}
}

