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
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>

#include "websock.h"
#include "afb-ws.h"

#include "utils-upoll.h"

static ssize_t aws_writev(struct afb_ws *ws, const struct iovec *iov, int iovcnt);
static ssize_t aws_readv(struct afb_ws *ws, const struct iovec *iov, int iovcnt);
static void aws_disconnect(struct afb_ws *ws);
static void aws_on_close(struct afb_ws *ws, uint16_t code, size_t size);
static void aws_on_text(struct afb_ws *ws, int last, size_t size);
static void aws_on_binary(struct afb_ws *ws, int last, size_t size);
static void aws_on_continue(struct afb_ws *ws, int last, size_t size);
static void aws_on_readable(struct afb_ws *ws);
static void aws_on_hangup(struct afb_ws *ws);

static struct websock_itf aws_itf = {
	.writev = (void*)aws_writev,
	.readv = (void*)aws_readv,
	.disconnect = (void*)aws_disconnect,

	.on_ping = NULL,
	.on_pong = NULL,
	.on_close = (void*)aws_on_close,
	.on_text = (void*)aws_on_text,
	.on_binary = (void*)aws_on_binary,
	.on_continue = (void*)aws_on_continue,
	.on_extension = NULL
};

struct buf
{
	char *buffer;
	size_t size;
};

struct afb_ws
{
	int fd;
	enum { none, text, binary } type;
	const struct afb_ws_itf *itf;
	void *closure;
	struct websock *ws;
	struct upoll *up;
	struct buf buffer;
};

struct afb_ws *afb_ws_create(int fd, const struct afb_ws_itf *itf, void *closure)
{
	struct afb_ws *result;

	assert(fd >= 0);

	result = malloc(sizeof * result);
	if (result == NULL)
		goto error;

	result->fd = fd;
	result->type = none;
	result->itf = itf;
	result->closure = closure;

	result->ws = websock_create_v13(&aws_itf, result);
	if (result->ws == NULL)
		goto error2;

	result->up = upoll_open(result->fd, result);
	if (result->up == NULL)
		goto error3;

	result->buffer.buffer = NULL;
	result->buffer.size = 0;

	upoll_on_readable(result->up, (void*)aws_on_readable);
	upoll_on_hangup(result->up, (void*)aws_on_hangup);

	return result;

error3:
	websock_destroy(result->ws);
error2:
	free(result);
error:
	close(fd);
	return NULL;
}

void afb_ws_disconnect(struct afb_ws *ws)
{
	struct upoll *up = ws->up;
	struct websock *wsi = ws->ws;
	ws->up = NULL;
	ws->ws = NULL;
	upoll_close(up);
	websock_destroy(wsi);
}

void afb_ws_close(struct afb_ws *ws, uint16_t code)
{
	websock_close_code(ws->ws, code);
}

void afb_ws_text(struct afb_ws *ws, const char *text, size_t length)
{
	websock_text(ws->ws, text, length);
}

void afb_ws_binary(struct afb_ws *ws, const void *data, size_t length)
{
	websock_binary(ws->ws, data, length);
}

static ssize_t aws_writev(struct afb_ws *ws, const struct iovec *iov, int iovcnt)
{
	ssize_t rc;
	do {
		rc = writev(ws->fd, iov, iovcnt);
	} while(rc == -1 && errno == EINTR);
	return rc;
}

static ssize_t aws_readv(struct afb_ws *ws, const struct iovec *iov, int iovcnt)
{
	ssize_t rc;
	do {
		rc = readv(ws->fd, iov, iovcnt);
	} while(rc == -1 && errno == EINTR);
	return rc;
}

static void aws_on_readable(struct afb_ws *ws)
{
	websock_dispatch(ws->ws);
}

static void aws_on_hangup(struct afb_ws *ws)
{
	afb_ws_disconnect(ws);
}

static void aws_disconnect(struct afb_ws *ws)
{
	afb_ws_disconnect(ws);
}

static inline struct buf aws_pick_buffer(struct afb_ws *ws)
{
	struct buf result = ws->buffer;
	ws->buffer.buffer = NULL;
	ws->buffer.size = 0;
	return result;
}

static int aws_read(struct afb_ws *ws, size_t size)
{
	ssize_t sz;
	char *buffer;

	if (size != 0) {
		buffer = realloc(ws->buffer.buffer, ws->buffer.size + size + 1);
		if (buffer == NULL)
			return 0;
		ws->buffer.buffer = buffer;
		sz = websock_read(ws->ws, &buffer[ws->buffer.size], size);
		if ((size_t)sz != size)
			return 0;
		ws->buffer.size += size;
	}
	return 1;
}

static void aws_on_close(struct afb_ws *ws, uint16_t code, size_t size)
{
	struct buf b;

	ws->type = none;
	if (ws->itf->on_close == NULL)
		websock_drop(ws->ws);
	else {
		aws_read(ws, size);
		b = aws_pick_buffer(ws);
		ws->itf->on_close(ws->closure, code, b.buffer, b.size);
	}
}

static void aws_on_text(struct afb_ws *ws, int last, size_t size)
{
	if (ws->type != none) {
		websock_drop(ws->ws);
		websock_close_code(ws->ws, WEBSOCKET_CODE_PROTOCOL_ERROR);
	} else if (ws->itf->on_text == NULL) {
		websock_drop(ws->ws);
		websock_close_code(ws->ws, WEBSOCKET_CODE_CANT_ACCEPT);
	} else {
		ws->type = text;
		aws_on_continue(ws, last, size);
	}
}

static void aws_on_binary(struct afb_ws *ws, int last, size_t size)
{
	if (ws->type != none) {
		websock_drop(ws->ws);
		websock_close_code(ws->ws, WEBSOCKET_CODE_PROTOCOL_ERROR);
	} else if (ws->itf->on_binary == NULL) {
		websock_drop(ws->ws);
		websock_close_code(ws->ws, WEBSOCKET_CODE_CANT_ACCEPT);
	} else {
		ws->type = text;
		aws_on_continue(ws, last, size);
	}
}

static void aws_on_continue(struct afb_ws *ws, int last, size_t size)
{
	struct buf b;
	int istxt;

	if (ws->type == none) {
		websock_drop(ws->ws);
		websock_close_code(ws->ws, WEBSOCKET_CODE_PROTOCOL_ERROR);
	} else {
		if (!aws_read(ws, size)) {
			aws_on_close(ws, WEBSOCKET_CODE_ABNORMAL, 0);
		} else if (last) {
			istxt = ws->type == text;
			ws->type = none;
			b = aws_pick_buffer(ws);
			b.buffer[b.size] = 0;
			(istxt ? ws->itf->on_text : ws->itf->on_binary)(ws->closure, b.buffer, b.size);
		}
	}
}

