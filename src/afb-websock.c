/*
 * Copyright 2016 IoT.bzh
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * Inspired by the work of 
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
#include <microhttpd.h>
#include <assert.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>

#include <json.h>

#include <openssl/sha.h>

#include "websock.h"

#include "afb-req-itf.h"
#include "afb-method.h"
#include "afb-hreq.h"
#include "afb-websock.h"
#include "afb-apis.h"
#include "session.h"
#include "utils-upoll.h"

/**************** WebSocket connection upgrade ****************************/

static const char websocket_s[] = "websocket";
static const char sec_websocket_key_s[] = "Sec-WebSocket-Key";
static const char sec_websocket_version_s[] = "Sec-WebSocket-Version";
static const char sec_websocket_accept_s[] = "Sec-WebSocket-Accept";
static const char sec_websocket_protocol_s[] = "Sec-WebSocket-Protocol";
static const char websocket_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static void enc64(unsigned char *in, char *out)
{
	static const char tob64[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";
	out[0] = tob64[in[0] >> 2];
	out[1] = tob64[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
	out[2] = tob64[((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6)];
	out[3] = tob64[in[2] & 0x3f];
}

static void make_accept_value(const char *key, char result[29])
{
	unsigned char md[SHA_DIGEST_LENGTH+1];
	size_t len = strlen(key);
	char *buffer = alloca(len + sizeof websocket_guid - 1);
	memcpy(buffer, key, len);
	memcpy(buffer + len, websocket_guid, sizeof websocket_guid - 1);
	SHA1((const unsigned char *)buffer, (unsigned long)(len + sizeof websocket_guid - 1), md);
	assert(SHA_DIGEST_LENGTH == 20);
	md[20] = 0;
	enc64(&md[0], &result[0]);
	enc64(&md[3], &result[4]);
	enc64(&md[6], &result[8]);
	enc64(&md[9], &result[12]);
	enc64(&md[12], &result[16]);
	enc64(&md[15], &result[20]);
	enc64(&md[18], &result[24]);
	result[27] = '=';
	result[28] = 0;
}

static int headerhas(const char *header, const char *needle)
{
	static const char sep[] = " \t,";
	size_t len, n;

	n = strlen(needle);
	for(;;) {
		header += strspn(header, sep);
		if (!*header)
			return 0;
		len = strcspn(header, sep);
		if (n == len && 0 == strncasecmp(needle, header, n))
			return 1;
		header += len;
	}
}

int afb_websock_check(struct afb_hreq *hreq, int *later)
{
	const char *connection, *upgrade, *key, *version, *protocols;
	char acceptval[29];
	int vernum;
	struct MHD_Response *response;

	/* is an upgrade to websocket ? */
	upgrade = afb_hreq_get_header(hreq, MHD_HTTP_HEADER_UPGRADE);
	if (upgrade == NULL || strcasecmp(upgrade, websocket_s))
		return 0;

	/* is a connection for upgrade ? */
	connection = afb_hreq_get_header(hreq, MHD_HTTP_HEADER_CONNECTION);
	if (connection == NULL || !headerhas (connection, MHD_HTTP_HEADER_UPGRADE))
		return 0;

	/* is a get ? */
	if(hreq->method != afb_method_get || strcasecmp(hreq->version, MHD_HTTP_VERSION_1_1))
		return 0;

	/* has a key and a version ? */
	key = afb_hreq_get_header(hreq, sec_websocket_key_s);
	version = afb_hreq_get_header(hreq, sec_websocket_version_s);
	if (key == NULL || version == NULL)
		return 0;

	/* is a supported version ? */
	vernum = atoi(version);
	if (vernum != 13) {
		response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header (response, sec_websocket_version_s, "13");
		MHD_queue_response (hreq->connection, MHD_HTTP_BAD_REQUEST, response);
		MHD_destroy_response (response);
		*later = 1;
		return 1;
	}

	/* is the protocol supported ? */
	protocols = afb_hreq_get_header(hreq, sec_websocket_protocol_s);

	/* send the accept connection */
	make_accept_value(key, acceptval);
	response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
	MHD_add_response_header (response, sec_websocket_accept_s, acceptval);
	MHD_add_response_header (response, MHD_HTTP_HEADER_CONNECTION, MHD_HTTP_HEADER_UPGRADE);
	MHD_add_response_header (response, MHD_HTTP_HEADER_UPGRADE, websocket_s);
	MHD_queue_response (hreq->connection, MHD_HTTP_SWITCHING_PROTOCOLS, response);
	MHD_destroy_response (response);

	*later = 0;
	return 1;
}

/**************** WebSocket handling ****************************/

static ssize_t aws_writev(struct afb_websock *ws, const struct iovec *iov, int iovcnt);
static ssize_t aws_readv(struct afb_websock *ws, const struct iovec *iov, int iovcnt);
static void aws_disconnect(struct afb_websock *ws);
static void aws_on_close(struct afb_websock *ws, uint16_t code, size_t size);
static void aws_on_content(struct afb_websock *ws, int last, size_t size);
static void aws_on_readable(struct afb_websock *ws);

static struct websock_itf aws_itf = {
	.writev = (void*)aws_writev,
	.readv = (void*)aws_readv,
	.disconnect = (void*)aws_disconnect,

	.on_ping = NULL,
	.on_pong = NULL,
	.on_close = (void*)aws_on_close,
	.on_text = (void*)aws_on_content,
	.on_binary = (void*)aws_on_content,
	.on_continue = (void*)aws_on_content
};

struct afb_wsreq
{
	struct afb_websock *aws;
	struct afb_wsreq *next;
	struct json_object *id;
	struct json_object *name;
	struct json_object *token;
	struct json_object *request;
};

struct afb_websock
{
	int fd;
	struct MHD_Connection *connection;
	struct websock *ws;
	struct upoll *up;
	struct AFB_clientCtx *context;
	struct json_tokener *tokener;
	struct afb_wsreq *requests;
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

struct afb_websock *afb_websock_create(struct afb_hreq *hreq)
{
	int fd;
	struct afb_websock *result;

	fd = MHD_get_connection_info(hreq->connection,
				MHD_CONNECTION_INFO_CONNECTION_FD)->connect_fd;
	fd = dup(fd);
	if (fd < 0)
		return NULL;

	result = malloc(sizeof * result);
	if (result == NULL)
		goto error;

	result->connection = hreq->connection;
	result->fd = fd;
	result->context = ctxClientGet(afb_hreq_context(hreq));
	if (result->context == NULL)
		goto error2;

	result->tokener = json_tokener_new();
	if (result->tokener == NULL)
		goto error2;

	result->ws = websock_create(&aws_itf, result);
	if (result->ws == NULL)
		goto error3;

	result->up = upoll_open(result->fd, result);
	if (result->up == NULL)
		goto error4;

	upoll_on_readable(result->up, (void*)aws_on_readable);
	upoll_on_hangup(result->up, (void*)aws_disconnect);
	return result;
error4:
	websock_destroy(result->ws);
error3:
	json_tokener_free(result->tokener);
error2:
	free(result);
error:
	close(fd);
	return NULL;
}

static ssize_t aws_writev(struct afb_websock *ws, const struct iovec *iov, int iovcnt)
{
	ssize_t rc;
	do {
		rc = writev(ws->fd, iov, iovcnt);
	} while(rc == -1 && errno == EINTR);
	return rc;
}

static ssize_t aws_readv(struct afb_websock *ws, const struct iovec *iov, int iovcnt)
{
	ssize_t rc;
	do {
		rc = readv(ws->fd, iov, iovcnt);
	} while(rc == -1 && errno == EINTR);
	return rc;
}

static void aws_disconnect(struct afb_websock *ws)
{
	upoll_close(ws->up);
	websock_destroy(ws->ws);
	close(ws->fd);
	MHD_resume_connection (ws->connection);
	ctxClientPut(ws->context);
	json_tokener_free(ws->tokener);
	free(ws);
}

static void aws_on_close(struct afb_websock *ws, uint16_t code, size_t size)
{
	/* do nothing */
}

static void aws_on_readable(struct afb_websock *ws)
{
	websock_dispatch(ws->ws);
}

static int aws_handle_json(struct afb_websock *aws, struct json_object *obj)
{
	struct afb_req r;
	int count, num, rc;
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
	rc = afb_apis_handle(r, aws->context, api, lenapi, verb, lenverb);
	if (rc == 0)
		wsreq_fail(wsreq, "ail", "api not found");
	return 1;

error:
	json_object_put(obj);
	return 0;
}

static void aws_on_content(struct afb_websock *ws, int last, size_t size)
{
	ssize_t rrc;
	char buffer[8000];
	struct json_object *obj;

	json_tokener_reset(ws->tokener);
	while(size) {
		rrc = websock_read(ws->ws, buffer,
				size > sizeof buffer ? sizeof buffer : size);
		if (rrc < 0) {
			websock_close(ws->ws);
			return;
		}
		size -= (size_t)rrc;
		obj = json_tokener_parse_ex(ws->tokener, buffer, (int)rrc);
		if (obj != NULL) {
			if (!aws_handle_json(ws, obj)) {
				websock_close(ws->ws);
				return;
			}
		} else if (json_tokener_get_error(ws->tokener) != json_tokener_continue) {
			websock_close(ws->ws);
			return;
		}
	}
}


static struct afb_arg wsreq_get(struct afb_wsreq *wsreq, const char *name)
{
	struct afb_arg arg;
	struct json_object *value;

	if (json_object_object_get_ex(wsreq->request, name, &value)) {
		arg.name = name;
		arg.value = json_object_get_string(value);
	} else {
		arg.name = NULL;
		arg.value = NULL;
	}
	arg.size = 0;
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
	websock_text(wsreq->aws->ws, message, strlen(message));
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

