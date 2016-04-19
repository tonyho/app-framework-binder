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
	/* do nothing but free the text */
	free(text);
}

#define CALL 2
#define RETOK 3
#define RETERR 4

struct afb_wsreq
{
	struct afb_ws_json *aws;
	struct afb_wsreq *next;
	char *text;
	size_t size;
	int code;
	char *id;
	size_t idlen;
	char *api;
	size_t apilen;
	char *verb;
	size_t verblen;
	char *obj;
	size_t objlen;
	char *tok;
	size_t toklen;
	struct json_object *root;
};

static struct json_object *wsreq_json(struct afb_wsreq *wsreq);
static struct afb_arg wsreq_get(struct afb_wsreq *wsreq, const char *name);
static void wsreq_fail(struct afb_wsreq *wsreq, const char *status, const char *info);
static void wsreq_success(struct afb_wsreq *wsreq, struct json_object *obj, const char *info);
static const char *wsreq_raw(struct afb_wsreq *wsreq, size_t *size);
static void wsreq_send(struct afb_wsreq *wsreq, char *buffer, size_t size);
static int wsreq_session_create(struct afb_wsreq *wsreq);
static int wsreq_session_check(struct afb_wsreq *wsreq, int refresh);
static void wsreq_session_close(struct afb_wsreq *wsreq);


static const struct afb_req_itf wsreq_itf = {
	.json = (void*)wsreq_json,
	.get = (void*)wsreq_get,
	.success = (void*)wsreq_success,
	.fail = (void*)wsreq_fail,
	.raw = (void*)wsreq_raw,
	.send = (void*)wsreq_send,
	.session_create = (void*)wsreq_session_create,
	.session_check = (void*)wsreq_session_check,
	.session_close = (void*)wsreq_session_close,
	.context_get = (void*)afb_context_get,
	.context_set = (void*)afb_context_set

};

static int aws_wsreq_parse(struct afb_wsreq *r, char *text, size_t size)
{
	char *pos, *end, c;
	int aux;

	/* scan */
	pos = text;
	end = text + size;

	/* scans: [ */
	while(pos < end && *pos == ' ') pos++;
	if (pos == end) goto bad_header;
	if (*pos++ != '[') goto bad_header;

	/* scans code: 2|3|4 */
	while(pos < end && *pos == ' ') pos++;
	if (pos == end) goto bad_header;
	switch (*pos++) {
	case '2': r->code = CALL; break;
	case '3': r->code = RETOK; break;
	case '4': r->code = RETERR; break;
	default: goto bad_header;
	}

	/* scans: , */
	while(pos < end && *pos == ' ') pos++;
	if (pos == end) goto bad_header;
	if (*pos++ != ',') goto bad_header;

	/* scans id: "id" */
	while(pos < end && *pos == ' ') pos++;
	if (pos == end) goto bad_header;
	if (*pos++ != '"') goto bad_header;
	r->id = pos;
	while(pos < end && *pos != '"') pos++;
	if (pos == end) goto bad_header;
	r->idlen = (size_t)(pos++ - r->id);

	/* scans: , */
	while(pos < end && *pos == ' ') pos++;
	if (pos == end) goto bad_header;
	if (*pos++ != ',') goto bad_header;

	/* scans the method if needed */
	if (r->code == CALL) {
		/* scans: " */
		while(pos < end && *pos == ' ') pos++;
		if (pos == end) goto bad_header;
		if (*pos++ != '"') goto bad_header;

		/* scans: api/ */
		r->api = pos;
		while(pos < end && *pos != '"' && *pos != '/') pos++;
		if (pos == end) goto bad_header;
		if (*pos != '/') goto bad_header;
		r->apilen = (size_t)(pos++ - r->api);
		if (r->apilen && r->api[r->apilen - 1] == '\\')
			r->apilen--;

		/* scans: verb" */
		r->verb = pos;
		while(pos < end && *pos != '"') pos++;
		if (pos == end) goto bad_header;
		r->verblen = (size_t)(pos++ - r->verb);

		/* scans: , */
		while(pos < end && *pos == ' ') pos++;
		if (pos == end) goto bad_header;
		if (*pos++ != ',') goto bad_header;
	}

	/* scan obj */
	while(pos < end && *pos == ' ') pos++;
	if (pos == end) goto bad_header;
	aux = 0;
	r->obj = pos;
	while (pos < end && (aux != 0 || (*pos != ',' && *pos != ']'))) {
		if (pos == end) goto bad_header;
		switch(*pos) {
		case '{': case '[': aux++; break;
		case '}': case ']': if (!aux--) goto bad_header; break;
		case '"':
			do {
				pos += 1 + (*pos == '\\');
			} while(pos < end && *pos != '"');
		default:
			break;
		}
		pos++;
	}
	if (pos > end) goto bad_header;
	if (pos == end && aux != 0) goto bad_header;
	c = *pos;
	r->objlen = (size_t)(pos++ - r->obj);
	while (r->objlen && r->obj[r->objlen - 1] == ' ')
		r->objlen--;

	/* scan the token (if any) */
	if (c == ',') {
		/* scans token: "token" */
		while(pos < end && *pos == ' ') pos++;
		if (pos == end) goto bad_header;
		if (*pos++ != '"') goto bad_header;
		r->tok = pos;
		while(pos < end && *pos != '"') pos++;
		if (pos == end) goto bad_header;
		r->toklen = (size_t)(pos++ - r->tok);
		while(pos < end && *pos == ' ') pos++;
		if (pos == end) goto bad_header;
		c = *pos++;
	}

	/* scan: ] */
	if (c != ']') goto bad_header;
	while(pos < end && *pos == ' ') pos++;
	if (pos != end) goto bad_header;

	/* done */
	r->text = text;
	r->size = size;
fprintf(stderr, "\n\nONTEXT([%d, %.*s, %.*s/%.*s, %.*s, %.*s])\n\n",
	r->code,
	(int)r->idlen, r->id,
	(int)r->apilen, r->api,
	(int)r->verblen, r->verb,
	(int)r->objlen, r->obj,
	(int)r->toklen, r->tok
);
	return 1;

bad_header:
	return 0;
}

static void aws_on_text(struct afb_ws_json *ws, char *text, size_t size)
{
	struct afb_req r;
	struct afb_wsreq *wsreq;

	/* allocate */
	wsreq = calloc(1, sizeof *wsreq);
	if (wsreq == NULL)
		goto alloc_error;

	/* init */
	if (!aws_wsreq_parse(wsreq, text, size))
		goto bad_header;

	/* fill and record the request */
	wsreq->aws = ws;
	wsreq->next = ws->requests;
	ws->requests = wsreq;

	r.req_closure = wsreq;
	r.itf = &wsreq_itf;
	afb_apis_call(r, ws->context, wsreq->api, wsreq->apilen, wsreq->verb, wsreq->verblen);
	return;

bad_header:
	free(wsreq);
alloc_error:
	free(text);
	afb_ws_close(ws->ws, 1008);
	return;
}

static struct json_object *wsreq_json(struct afb_wsreq *wsreq)
{
	struct json_object *root = wsreq->root;
	if (root == NULL) {
		json_tokener_reset(wsreq->aws->tokener);
		root = json_tokener_parse_ex(wsreq->aws->tokener, wsreq->obj, (int)wsreq->objlen);
		if (root == NULL) {
			/* lazy discovering !!!! not good TODO improve*/
			root = json_object_new_object();
		}
		wsreq->root = root;
	}
	return root;
}

static struct afb_arg wsreq_get(struct afb_wsreq *wsreq, const char *name)
{
	struct afb_arg arg;
	struct json_object *value, *root;

	root = wsreq_json(wsreq);
	if (json_object_object_get_ex(root, name, &value)) {
		arg.name = name;
		arg.value = json_object_get_string(value);
	} else {
		arg.name = NULL;
		arg.value = NULL;
	}
	arg.path = NULL;
	return arg;
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
	struct AFB_clientCtx *context = wsreq->aws->context;

	if (wsreq->tok == NULL)
		return 0;

	if (!ctxTokenCheckLen (context, wsreq->tok, wsreq->toklen))
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
	json_object_array_add(reply, json_object_new_string_len(wsreq->id, (int)wsreq->idlen));
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
	wsreq_reply(wsreq, RETERR, status, info, NULL);
}

static void wsreq_success(struct afb_wsreq *wsreq, json_object *obj, const char *info)
{
	wsreq_reply(wsreq, RETOK, "success", info, obj);
}

static const char *wsreq_raw(struct afb_wsreq *wsreq, size_t *size)
{
	*size = wsreq->objlen;
	return wsreq->obj;
}

static void wsreq_send(struct afb_wsreq *wsreq, char *buffer, size_t size)
{
	afb_ws_text(wsreq->aws->ws, buffer, size);
}

