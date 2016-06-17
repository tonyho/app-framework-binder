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
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <json-c/json.h>

#include <afb/afb-req-itf.h>

#include "afb-wsj1.h"
#include "afb-ws-json1.h"
#include "afb-msg-json.h"
#include "session.h"
#include "afb-apis.h"
#include "afb-context.h"
#include "afb-evt.h"
#include "afb-subcall.h"
#include "verbose.h"

/* predeclaration of structures */
struct afb_ws_json1;
struct afb_wsreq;

/* predeclaration of websocket callbacks */
static void aws_on_hangup(struct afb_ws_json1 *ws, struct afb_wsj1 *wsj1);
static void aws_on_call(struct afb_ws_json1 *ws, const char *api, const char *verb, struct afb_wsj1_msg *msg);
static void aws_on_event(struct afb_ws_json1 *ws, const char *event, int eventid, struct json_object *object);

/* predeclaration of wsreq callbacks */
static void wsreq_addref(struct afb_wsreq *wsreq);
static void wsreq_unref(struct afb_wsreq *wsreq);
static struct json_object *wsreq_json(struct afb_wsreq *wsreq);
static struct afb_arg wsreq_get(struct afb_wsreq *wsreq, const char *name);
static void wsreq_fail(struct afb_wsreq *wsreq, const char *status, const char *info);
static void wsreq_success(struct afb_wsreq *wsreq, struct json_object *obj, const char *info);
static const char *wsreq_raw(struct afb_wsreq *wsreq, size_t *size);
static void wsreq_send(struct afb_wsreq *wsreq, const char *buffer, size_t size);
static int wsreq_subscribe(struct afb_wsreq *wsreq, struct afb_event event);
static int wsreq_unsubscribe(struct afb_wsreq *wsreq, struct afb_event event);
static void wsreq_subcall(struct afb_wsreq *wsreq, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure);

/* declaration of websocket structure */
struct afb_ws_json1
{
	int refcount;
	void (*cleanup)(void*);
	void *cleanup_closure;
	struct AFB_clientCtx *session;
	struct afb_evt_listener *listener;
	struct afb_wsj1 *wsj1;
	int new_session;
};

/* declaration of wsreq structure */
struct afb_wsreq
{
	/*
	 * CAUTION: 'context' field should be the first because there
	 * is an implicit convertion to struct afb_context
	 */
	struct afb_context context;
	int refcount;
	struct afb_ws_json1 *aws;
	struct afb_wsreq *next;
	struct afb_wsj1_msg *msgj1;
};

/* interface for afb_ws_json1 / afb_wsj1 */
static struct afb_wsj1_itf wsj1_itf = {
	.on_hangup = (void*)aws_on_hangup,
	.on_call = (void*)aws_on_call
};

/* interface for wsreq / afb_req */
const struct afb_req_itf afb_ws_json1_req_itf = {
	.json = (void*)wsreq_json,
	.get = (void*)wsreq_get,
	.success = (void*)wsreq_success,
	.fail = (void*)wsreq_fail,
	.raw = (void*)wsreq_raw,
	.send = (void*)wsreq_send,
	.context_get = (void*)afb_context_get,
	.context_set = (void*)afb_context_set,
	.addref = (void*)wsreq_addref,
	.unref = (void*)wsreq_unref,
	.session_close = (void*)afb_context_close,
	.session_set_LOA = (void*)afb_context_change_loa,
	.subscribe = (void*)wsreq_subscribe,
	.unsubscribe = (void*)wsreq_unsubscribe,
	.subcall = (void*)wsreq_subcall
};

/* the interface for events */
static const struct afb_evt_itf evt_itf = {
	.broadcast = (void*)aws_on_event,
	.push = (void*)aws_on_event
};

/***************************************************************
****************************************************************
**
**  functions of afb_ws_json1 / afb_wsj1
**
****************************************************************
***************************************************************/

struct afb_ws_json1 *afb_ws_json1_create(int fd, struct afb_context *context, void (*cleanup)(void*), void *cleanup_closure)
{
	struct afb_ws_json1 *result;

	assert(fd >= 0);
	assert(context != NULL);

	result = malloc(sizeof * result);
	if (result == NULL)
		goto error;

	result->refcount = 1;
	result->cleanup = cleanup;
	result->cleanup_closure = cleanup_closure;
	result->session = ctxClientAddRef(context->session);
	result->new_session = context->created != 0;
	if (result->session == NULL)
		goto error2;

	result->wsj1 = afb_wsj1_create(fd, &wsj1_itf, result);
	if (result->wsj1 == NULL)
		goto error3;

	result->listener = afb_evt_listener_create(&evt_itf, result);
	if (result->listener == NULL)
		goto error4;

	return result;

error4:
	afb_wsj1_unref(result->wsj1);
error3:
	ctxClientUnref(result->session);
error2:
	free(result);
error:
	close(fd);
	return NULL;
}

static struct afb_ws_json1 *aws_addref(struct afb_ws_json1 *ws)
{
	ws->refcount++;
	return ws;
}

static void aws_unref(struct afb_ws_json1 *ws)
{
	if (--ws->refcount == 0) {
		afb_evt_listener_unref(ws->listener);
		afb_wsj1_unref(ws->wsj1);
		if (ws->cleanup != NULL)
			ws->cleanup(ws->cleanup_closure);
		ctxClientUnref(ws->session);
		free(ws);
	}
}

static void aws_on_hangup(struct afb_ws_json1 *ws, struct afb_wsj1 *wsj1)
{
	aws_unref(ws);
}

static void aws_on_call(struct afb_ws_json1 *ws, const char *api, const char *verb, struct afb_wsj1_msg *msg)
{
	struct afb_req r;
	struct afb_wsreq *wsreq;

	DEBUG("received websocket request for %s/%s: %s", api, verb, afb_wsj1_msg_object_s(msg));

	/* allocate */
	wsreq = calloc(1, sizeof *wsreq);
	if (wsreq == NULL) {
		afb_wsj1_close(ws->wsj1, 1008, NULL);
		return;
	}

	/* init the context */
	afb_context_init(&wsreq->context, ws->session, afb_wsj1_msg_token(msg));
	if (!wsreq->context.invalidated)
		wsreq->context.validated = 1;
	if (ws->new_session != 0) {
		wsreq->context.created = 1;
		ws->new_session = 0;
	}

	/* fill and record the request */
	afb_wsj1_msg_addref(msg);
	wsreq->msgj1 = msg;
	wsreq->refcount = 1;
	wsreq->aws = aws_addref(ws);

	/* emits the call */
	r.closure = wsreq;
	r.itf = &afb_ws_json1_req_itf;
	afb_apis_call_(r, &wsreq->context, api, verb);
	wsreq_unref(wsreq);
}

static void aws_on_event(struct afb_ws_json1 *aws, const char *event, int eventid, struct json_object *object)
{
	afb_wsj1_send_event_j(aws->wsj1, event, afb_msg_json_event(event, object));
}

/***************************************************************
****************************************************************
**
**  functions of wsreq / afb_req
**
****************************************************************
***************************************************************/

static void wsreq_addref(struct afb_wsreq *wsreq)
{
	wsreq->refcount++;
}

static void wsreq_unref(struct afb_wsreq *wsreq)
{
	if (--wsreq->refcount == 0) {
		afb_context_disconnect(&wsreq->context);
		afb_wsj1_msg_unref(wsreq->msgj1);
		aws_unref(wsreq->aws);
		free(wsreq);
	}
}

static struct json_object *wsreq_json(struct afb_wsreq *wsreq)
{
	return afb_wsj1_msg_object_j(wsreq->msgj1);
}

static struct afb_arg wsreq_get(struct afb_wsreq *wsreq, const char *name)
{
	return afb_msg_json_get_arg(wsreq_json(wsreq), name);
}

static void wsreq_fail(struct afb_wsreq *wsreq, const char *status, const char *info)
{
	int rc;
	rc = afb_wsj1_reply_error_j(wsreq->msgj1, afb_msg_json_reply_error(status, info, &wsreq->context, NULL), afb_context_sent_token(&wsreq->context));
	if (rc)
		ERROR("Can't send fail reply: %m");
}

static void wsreq_success(struct afb_wsreq *wsreq, json_object *obj, const char *info)
{
	int rc;
	rc = afb_wsj1_reply_ok_j(wsreq->msgj1, afb_msg_json_reply_ok(info, obj, &wsreq->context, NULL), afb_context_sent_token(&wsreq->context));
	if (rc)
		ERROR("Can't send success reply: %m");
}

static const char *wsreq_raw(struct afb_wsreq *wsreq, size_t *size)
{
	const char *result = afb_wsj1_msg_object_s(wsreq->msgj1);
	if (size != NULL)
		*size = strlen(result);
	return result;
}

static void wsreq_send(struct afb_wsreq *wsreq, const char *buffer, size_t size)
{
	int rc;
	rc = afb_wsj1_reply_ok_s(wsreq->msgj1, buffer, afb_context_sent_token(&wsreq->context));
	if (rc)
		ERROR("Can't send raw reply: %m");
}

static int wsreq_subscribe(struct afb_wsreq *wsreq, struct afb_event event)
{
	return afb_evt_add_watch(wsreq->aws->listener, event);
}

static int wsreq_unsubscribe(struct afb_wsreq *wsreq, struct afb_event event)
{
	return afb_evt_remove_watch(wsreq->aws->listener, event);
}

static void wsreq_subcall(struct afb_wsreq *wsreq, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure)
{
	afb_subcall(&wsreq->context, api, verb, args, callback, closure, (struct afb_req){ .itf = &afb_ws_json1_req_itf, .closure = wsreq });
}

