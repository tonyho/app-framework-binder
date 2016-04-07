/*
 * Copyright 2016 IoT.bzh
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

#include <json.h>

#include "afb-ws.h"
#include "afb-ws-json.h"
#include "session.h"
#include "afb-req-itf.h"
#include "afb-apis.h"

static void aws_on_close(struct afb_ws_json *ws, uint16_t code, char *text, size_t size);
static void aws_on_text(struct afb_ws_json *ws, char *text, size_t size);

static struct afb_ws_itf aws_itf = {
	.on_close = (void*)aws_on_close,
	.on_text = (void*)aws_on_text,
	.on_binary = NULL,
};

struct afb_wsreq;

struct afb_ws_json
{
	void (*cleanup)(void*);
	void *cleanup_closure;
	struct afb_wsreq *requests;
	struct AFB_clientCtx *context;
	struct json_tokener *tokener;
	struct afb_ws *ws;
};

struct afb_ws_json *afb_ws_json_create(int fd, struct AFB_clientCtx *context, void (*cleanup)(void*), void *closure)
{
	struct afb_ws_json *result;

	assert(fd >= 0);
	assert(context != NULL);

	result = malloc(sizeof * result);
	if (result == NULL)
		goto error;

	result->cleanup = cleanup;
	result->cleanup_closure = closure;
	result->requests = NULL;
	result->context = ctxClientGet(context);
	if (result->context == NULL)
		goto error2;

	result->tokener = json_tokener_new();
	if (result->tokener == NULL)
		goto error3;

	result->ws = afb_ws_create(fd, &aws_itf, result);
	if (result->ws == NULL)
		goto error4;

	return result;

error4:
	json_tokener_free(result->tokener);
error3:
	ctxClientPut(result->context);
error2:
	free(result);
error:
	close(fd);
	return NULL;
}

static void aws_on_close(struct afb_ws_json *ws, uint16_t code, char *text, size_t size)
{
	/* do nothing */
	free(text);
}


struct afb_wsreq
{
	struct afb_ws_json *aws;
	struct afb_wsreq *next;
	struct json_object *id;
	struct json_object *name;
	struct json_object *token;
	struct json_object *request;
};
static struct afb_arg wsreq_get(struct afb_wsreq *wsreq, const char *name);
static void wsreq_iterate(struct afb_wsreq *wsreq, int (*iterator)(void *closure, struct afb_arg arg), void *closure);
static void wsreq_fail(struct afb_wsreq *wsreq, const char *status, const char *info);
static void wsreq_success(struct afb_wsreq *wsreq, struct json_object *obj, const char *info);
static int wsreq_session_create(struct afb_wsreq *wsreq);
static int wsreq_session_check(struct afb_wsreq *wsreq, int refresh);
static void wsreq_session_close(struct afb_wsreq *wsreq);

static const struct afb_req_itf wsreq_itf = {
	.get = (void*)wsreq_get,
	.iterate = (void*)wsreq_iterate,
	.fail = (void*)wsreq_fail,
	.success = (void*)wsreq_success,
	.session_create = (void*)wsreq_session_create,
	.session_check = (void*)wsreq_session_check,
	.session_close = (void*)wsreq_session_close
};

static int aws_handle_json(struct afb_ws_json *aws, struct json_object *obj)
{
	struct afb_req r;
	int count, num;
	struct json_object *type, *id, *name, *req, *token;
	struct afb_wsreq *wsreq;
	const char *api, *verb;
	size_t lenapi, lenverb;

	/* protocol inspired by http://www.gir.fr/ocppjs/ocpp_srpc_spec.shtml */

	/* the object must be an array of 4 or 5 elements */
	if (!json_object_is_type(obj, json_type_array))
		goto error;
	count = json_object_array_length(obj);
	if (count < 4 || count > 5)
		goto error;

	/* get the 5 elements: type id name request token */
	type = json_object_array_get_idx(obj, 0);
	id = json_object_array_get_idx(obj, 1);
	name = json_object_array_get_idx(obj, 2);
	req = json_object_array_get_idx(obj, 3);
	token = json_object_array_get_idx(obj, 4);

	/* check the types: int string string object string */
	if (!json_object_is_type(type, json_type_int))
		goto error;
	if (!json_object_is_type(id, json_type_string))
		goto error;
	if (!json_object_is_type(name, json_type_string))
		goto error;
	if (!json_object_is_type(req, json_type_object))
		goto error;
	if (token != NULL && !json_object_is_type(token, json_type_string))
		goto error;

	/* the type is only 2 */
	num = json_object_get_int(type);
	if (num != 2)
		goto error;

	/* checks the api/verb structure of name */
	api = json_object_get_string(name);
	for (lenapi = 0 ; api[lenapi] && api[lenapi] != '/' ; lenapi++);
	if (!lenapi || !api[lenapi])
		goto error;
	verb = &api[lenapi+1];
	for (lenverb = 0 ; verb[lenverb] && verb[lenverb] != '/' ; lenverb++);
	if (!lenverb || verb[lenverb])
		goto error;

	/* allocates the request data */
	wsreq = malloc(sizeof *wsreq);
	if (wsreq == NULL)
		goto error;

	/* fill and record the request */
	wsreq->aws = aws;
	wsreq->id = json_object_get(id);
	wsreq->name = json_object_get(name);
	wsreq->token = json_object_get(token);
	wsreq->request = json_object_get(req);
	wsreq->next = aws->requests;
	aws->requests = wsreq;
	json_object_put(obj);

	r.data = wsreq;
	r.itf = &wsreq_itf;
	afb_apis_call(r, aws->context, api, lenapi, verb, lenverb);
	return 1;

error:
	json_object_put(obj);
	return 0;
}

static void aws_on_text(struct afb_ws_json *ws, char *text, size_t size)
{
	struct json_object *obj;
	json_tokener_reset(ws->tokener);
	obj = json_tokener_parse_ex(ws->tokener, text, (int)size);
	if (obj == NULL) {
		afb_ws_close(ws->ws, 1008);
	} else if (!aws_handle_json(ws, obj)) {
		afb_ws_close(ws->ws, 1008);
	}
}

static struct afb_arg wsreq_get(struct afb_wsreq *wsreq, const char *name)
{
	struct afb_arg arg;
	struct json_object *value;

	if (json_object_object_get_ex(wsreq->request, name, &value)) {
		arg.name = name;
		arg.value = json_object_get_string(value);
		arg.size = strlen(arg.value);
	} else {
		arg.name = NULL;
		arg.value = NULL;
		arg.size = 0;
	}
	arg.path = NULL;
	return arg;
}

static void wsreq_iterate(struct afb_wsreq *wsreq, int (*iterator)(void *closure, struct afb_arg arg), void *closure)
{
	struct afb_arg arg;
	struct json_object_iterator it = json_object_iter_begin(wsreq->request);
	struct json_object_iterator end = json_object_iter_end(wsreq->request);

	arg.size = 0;
	arg.path = NULL;
	while(!json_object_iter_equal(&it, &end)) {
		arg.name = json_object_iter_peek_name(&it);
		arg.value = json_object_get_string(json_object_iter_peek_value(&it));
		if (!iterator(closure, arg))
			break;
		json_object_iter_next(&it);
	}
}

static int wsreq_session_create(struct afb_wsreq *wsreq)
{
	struct AFB_clientCtx *context = wsreq->aws->context;
	if (context->created)
		return 0;
	return wsreq_session_check(wsreq, 1);
}

static int wsreq_session_check(struct afb_wsreq *wsreq, int refresh)
{
	const char *token;
	struct AFB_clientCtx *context = wsreq->aws->context;

	if (wsreq->token == NULL)
		return 0;

	token = json_object_get_string(wsreq->token);
	if (token == NULL)
		return 0;

	if (!ctxTokenCheck (context, token))
		return 0;

	if (refresh) {
		ctxTokenNew (context);
	}

	return 1;
}

static void wsreq_session_close(struct afb_wsreq *wsreq)
{
	struct AFB_clientCtx *context = wsreq->aws->context;
	ctxClientClose(context);
}


static void wsreq_reply(struct afb_wsreq *wsreq, int retcode, const char *status, const char *info, json_object *resp)
{
	json_object *root, *request, *reply;
	const char *message;

	/* builds the answering structure */
	root = json_object_new_object();
	json_object_object_add(root, "jtype", json_object_new_string("afb-reply"));
	request = json_object_new_object();
	json_object_object_add(root, "request", request);
	json_object_object_add(request, "status", json_object_new_string(status));
	if (info)
		json_object_object_add(request, "info", json_object_new_string(info));
	if (resp)
		json_object_object_add(root, "response", resp);

	/* make the reply */
	reply = json_object_new_array();
	json_object_array_add(reply, json_object_new_int(retcode));
	json_object_array_add(reply, wsreq->id);
	json_object_array_add(reply, root);
	json_object_array_add(reply, json_object_new_string(wsreq->aws->context->token));

	/* emits the reply */
	message = json_object_to_json_string(reply);
	afb_ws_text(wsreq->aws->ws, message, strlen(message));
	json_object_put(reply);

	/* TODO eliminates the wsreq */
}

static void wsreq_fail(struct afb_wsreq *wsreq, const char *status, const char *info)
{
	wsreq_reply(wsreq, 4, status, info, NULL);
}

static void wsreq_success(struct afb_wsreq *wsreq, json_object *obj, const char *info)
{
	wsreq_reply(wsreq, 3, "success", info, obj);
}

