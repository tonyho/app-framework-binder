/*
 * Copyright 2016 iot.bzh
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This work is a far adaptation of apache-websocket:
 *   origin:  https://github.com/disconnect/apache-websocket
 *   commit:  cfaef071223f11ba016bff7e1e4b7c9e5df45b50
 *   Copyright 2010-2012 self.disconnect (APACHE-2)
 */

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/uio.h>

#include "websock.h"

#define BLOCK_DATA_SIZE              4096

#define FRAME_GET_FIN(BYTE)         (((BYTE) >> 7) & 0x01)
#define FRAME_GET_RSV1(BYTE)        (((BYTE) >> 6) & 0x01)
#define FRAME_GET_RSV2(BYTE)        (((BYTE) >> 5) & 0x01)
#define FRAME_GET_RSV3(BYTE)        (((BYTE) >> 4) & 0x01)
#define FRAME_GET_OPCODE(BYTE)      ( (BYTE)       & 0x0F)
#define FRAME_GET_MASK(BYTE)        (((BYTE) >> 7) & 0x01)
#define FRAME_GET_PAYLOAD_LEN(BYTE) ( (BYTE)       & 0x7F)

#define FRAME_SET_FIN(BYTE)         (((BYTE) & 0x01) << 7)
#define FRAME_SET_OPCODE(BYTE)       ((BYTE) & 0x0F)
#define FRAME_SET_MASK(BYTE)        (((BYTE) & 0x01) << 7)
#define FRAME_SET_LENGTH(X64, IDX)  (unsigned char)(((X64) >> ((IDX)*8)) & 0xFF)

#define OPCODE_CONTINUATION 0x0
#define OPCODE_TEXT         0x1
#define OPCODE_BINARY       0x2
#define OPCODE_CLOSE        0x8
#define OPCODE_PING         0x9
#define OPCODE_PONG         0xA

#define STATE_INIT    0
#define STATE_START   1
#define STATE_LENGTH  2
#define STATE_DATA    3
#define STATE_CLOSED  4

struct websock {
	int state;
	uint64_t maxlength;
	int lenhead, szhead;
	uint64_t length;
	uint32_t mask;
	unsigned char header[14];	/* 2 + 8 + 4 */
	const struct websock_itf *itf;
	void *closure;
};

static ssize_t ws_writev(struct websock *ws, const struct iovec *iov, int iovcnt)
{
	return ws->itf->writev(ws->closure, iov, iovcnt);
}

static ssize_t ws_readv(struct websock *ws, const struct iovec *iov, int iovcnt)
{
	return ws->itf->readv(ws->closure, iov, iovcnt);
}

#if 0
static ssize_t ws_write(struct websock *ws, const void *buffer, size_t buffer_size)
{
	struct iovec iov;
	iov.iov_base = (void *)buffer;	/* const cast */
	iov.iov_len = buffer_size;
	return ws_writev(ws, &iov, 1);
}
#endif

static ssize_t ws_read(struct websock *ws, void *buffer, size_t buffer_size)
{
	struct iovec iov;
	iov.iov_base = buffer;
	iov.iov_len = buffer_size;
	return ws_readv(ws, &iov, 1);
}

static ssize_t websock_send(struct websock *ws, unsigned char opcode,
			    const void *buffer, size_t buffer_size)
{
	struct iovec iov[2];
	size_t pos;
	ssize_t rc;
	unsigned char header[32];

	if (ws->state == STATE_CLOSED)
		return 0;

	pos = 0;
	header[pos++] = (unsigned char)(FRAME_SET_FIN(1) | FRAME_SET_OPCODE(opcode));
	buffer_size = (uint64_t) buffer_size;
	if (buffer_size < 126) {
		header[pos++] =
		    FRAME_SET_MASK(0) | FRAME_SET_LENGTH(buffer_size, 0);
	} else {
		if (buffer_size < 65536) {
			header[pos++] = FRAME_SET_MASK(0) | 126;
		} else {
			header[pos++] = FRAME_SET_MASK(0) | 127;
			header[pos++] = FRAME_SET_LENGTH(buffer_size, 7);
			header[pos++] = FRAME_SET_LENGTH(buffer_size, 6);
			header[pos++] = FRAME_SET_LENGTH(buffer_size, 5);
			header[pos++] = FRAME_SET_LENGTH(buffer_size, 4);
			header[pos++] = FRAME_SET_LENGTH(buffer_size, 3);
			header[pos++] = FRAME_SET_LENGTH(buffer_size, 2);
		}
		header[pos++] = FRAME_SET_LENGTH(buffer_size, 1);
		header[pos++] = FRAME_SET_LENGTH(buffer_size, 0);
	}

	iov[0].iov_base = header;
	iov[0].iov_len = pos;
	iov[1].iov_base = (void *)buffer;	/* const cast */
	iov[1].iov_len = buffer_size;

	rc = ws_writev(ws, iov, 1 + !!buffer_size);

	if (opcode == OPCODE_CLOSE) {
		ws->length = 0;
		ws->state = STATE_CLOSED;
		ws->itf->disconnect(ws->closure);
	}
	return rc;
}

void websock_close(struct websock *ws)
{
	websock_send(ws, OPCODE_CLOSE, NULL, 0);
}

void websock_close_code(struct websock *ws, uint16_t code)
{
	unsigned char buffer[2];
	/* Send server-side closing handshake */
	buffer[0] = (unsigned char)((code >> 8) & 0xFF);
	buffer[1] = (unsigned char)(code & 0xFF);
	websock_send(ws, OPCODE_CLOSE, buffer, 2);
}

void websock_ping(struct websock *ws)
{
	websock_send(ws, OPCODE_PING, NULL, 0);
}

void websock_pong(struct websock *ws)
{
	websock_send(ws, OPCODE_PONG, NULL, 0);
}

void websock_text(struct websock *ws, const char *text, size_t length)
{
	websock_send(ws, OPCODE_TEXT, text, length);
}

void websock_binary(struct websock *ws, const void *data, size_t length)
{
	websock_send(ws, OPCODE_BINARY, data, length);
}

static int read_header(struct websock *ws)
{
	if (ws->lenhead < ws->szhead) {
		ssize_t rbc =
		    ws_read(ws, &ws->header[ws->lenhead], (size_t)(ws->szhead - ws->lenhead));
		if (rbc < 0)
			return -1;
		ws->lenhead += (int)rbc;
	}
	return 0;
}

int websock_dispatch(struct websock *ws)
{
loop:
	switch (ws->state) {
	case STATE_INIT:
		ws->lenhead = 0;
		ws->szhead = 2;
		ws->state = STATE_START;

	case STATE_START:
		/* read the header */
		if (read_header(ws))
			return -1;
		else if (ws->lenhead < ws->szhead)
			return 0;
		/* sanity checks */
		if (FRAME_GET_RSV1(ws->header[0]) != 0)
			goto protocol_error;
		if (FRAME_GET_RSV2(ws->header[0]) != 0)
			goto protocol_error;
		if (FRAME_GET_RSV3(ws->header[0]) != 0)
			goto protocol_error;
		/* fast track */
		switch (FRAME_GET_OPCODE(ws->header[0])) {
		case OPCODE_CONTINUATION:
		case OPCODE_TEXT:
		case OPCODE_BINARY:
			break;
		case OPCODE_CLOSE:
			if (FRAME_GET_MASK(ws->header[1]))
				goto protocol_error;
			if (FRAME_GET_PAYLOAD_LEN(ws->header[1]) == 1)
				goto protocol_error;
			if (FRAME_GET_PAYLOAD_LEN(ws->header[1]))
				ws->szhead += 2;
			break;
		case OPCODE_PING:
			if (FRAME_GET_MASK(ws->header[1]))
				goto protocol_error;
			if (FRAME_GET_PAYLOAD_LEN(ws->header[1]) != 0)
				goto protocol_error;
			if (ws->itf->on_ping)
				ws->itf->on_ping(ws->closure);
			else
				websock_pong(ws);
			ws->state = STATE_INIT;
			goto loop;
		case OPCODE_PONG:
			if (FRAME_GET_MASK(ws->header[1]))
				goto protocol_error;
			if (FRAME_GET_PAYLOAD_LEN(ws->header[1]) != 0)
				goto protocol_error;
			if (ws->itf->on_pong)
				ws->itf->on_pong(ws->closure);
			ws->state = STATE_INIT;
			goto loop;
		default:
			goto protocol_error;
		}
		/* update heading size */
		switch (FRAME_GET_PAYLOAD_LEN(ws->header[1])) {
		case 127:
			ws->szhead += 6;
		case 126:
			ws->szhead += 2;
		default:
			ws->szhead += 4 * FRAME_GET_MASK(ws->header[1]);
		}
		ws->state = STATE_LENGTH;

	case STATE_LENGTH:
		/* continue to read the header */
		if (read_header(ws))
			return -1;
		else if (ws->lenhead < ws->szhead)
			return 0;
		/* compute header values */
		switch (FRAME_GET_PAYLOAD_LEN(ws->header[1])) {
		case 127:
			ws->length = (((uint64_t) ws->header[2]) << 56)
			    | (((uint64_t) ws->header[3]) << 48)
			    | (((uint64_t) ws->header[4]) << 40)
			    | (((uint64_t) ws->header[5]) << 32)
			    | (((uint64_t) ws->header[6]) << 24)
			    | (((uint64_t) ws->header[7]) << 16)
			    | (((uint64_t) ws->header[8]) << 8)
			    | (uint64_t) ws->header[9];
			break;
		case 126:
			ws->length = (((uint64_t) ws->header[2]) << 8)
			    | (uint64_t) ws->header[3];
			break;
		default:
			ws->length = FRAME_GET_PAYLOAD_LEN(ws->header[1]);
			break;
		}
		if (ws->length > ws->maxlength)
			goto too_long_error;
		if (FRAME_GET_MASK(ws->header[1])) {
			((unsigned char *)&ws->mask)[0] = ws->header[ws->szhead - 4];
			((unsigned char *)&ws->mask)[1] = ws->header[ws->szhead - 3];
			((unsigned char *)&ws->mask)[2] = ws->header[ws->szhead - 2];
			((unsigned char *)&ws->mask)[3] = ws->header[ws->szhead - 1];
		} else
			ws->mask = 0;
		ws->state = STATE_DATA;
		switch (FRAME_GET_OPCODE(ws->header[0])) {
		case OPCODE_CONTINUATION:
			ws->itf->on_continue(ws->closure,
					     FRAME_GET_FIN(ws->header[0]),
					     (size_t) ws->length);
			break;
		case OPCODE_TEXT:
			ws->itf->on_text(ws->closure,
					 FRAME_GET_FIN(ws->header[0]),
					 (size_t) ws->length);
			break;
		case OPCODE_BINARY:
			ws->itf->on_binary(ws->closure,
					   FRAME_GET_FIN(ws->header[0]),
					   (size_t) ws->length);
			break;
		case OPCODE_CLOSE:
			ws->state = STATE_CLOSED;
			if (ws->length)
				ws->itf->on_close(ws->closure,
						  (uint16_t)((((uint16_t) ws-> header[2]) << 8) | ((uint16_t) ws->header[3])),
						  (size_t) ws->length);
			else
				ws->itf->on_close(ws->closure,
						  STATUS_CODE_UNSET, 0);
			ws->itf->disconnect(ws->closure);
			return 0;
		}
		break;

	case STATE_DATA:
		if (ws->length)
			return 0;
		ws->state = STATE_INIT;
		break;

	case STATE_CLOSED:
		return 0;
	}
	goto loop;

 too_long_error:
	websock_close_code(ws, STATUS_CODE_MESSAGE_TOO_LARGE);
	return 0;

 protocol_error:
	websock_close_code(ws, STATUS_CODE_PROTOCOL_ERROR);
	return 0;
}

ssize_t websock_read(struct websock * ws, void *buffer, size_t size)
{
	uint32_t mask, *b32;
	uint8_t m, *b8;
	ssize_t rc;

	if (ws->state != STATE_DATA && ws->state != STATE_CLOSED)
		return 0;

	if (size > ws->length)
		size = (size_t) ws->length;

	rc = ws_read(ws, buffer, size);
	if (rc > 0) {
		size = (size_t) rc;
		ws->length -= size;

		if (ws->mask) {
			mask = ws->mask;
			b8 = buffer;
			while (size && ((sizeof(uint32_t) - 1) & (uintptr_t) b8)) {
				m = ((uint8_t *) & mask)[0];
				((uint8_t *) & mask)[0] = ((uint8_t *) & mask)[1];
				((uint8_t *) & mask)[1] = ((uint8_t *) & mask)[2];
				((uint8_t *) & mask)[2] = ((uint8_t *) & mask)[3];
				((uint8_t *) & mask)[3] = m;
				*b8++ ^= m;
				size--;
			}
			b32 = (uint32_t *) b8;
			while (size >= sizeof(uint32_t)) {
				*b32++ ^= mask;
				size -= sizeof(uint32_t);
			}
			b8 = (uint8_t *) b32;
			while (size) {
				m = ((uint8_t *) & mask)[0];
				((uint8_t *) & mask)[0] = ((uint8_t *) & mask)[1];
				((uint8_t *) & mask)[1] = ((uint8_t *) & mask)[2];
				((uint8_t *) & mask)[2] = ((uint8_t *) & mask)[3];
				((uint8_t *) & mask)[3] = m;
				*b8++ ^= m;
				size--;
			}
			ws->mask = mask;
		}
	}
	return rc;
}

void websock_drop(struct websock *ws)
{
	char buffer[4096];

	while (ws->length && ws_read(ws, buffer, sizeof buffer) >= 0) ;
}

struct websock *websock_create(const struct websock_itf *itf, void *closure)
{
	struct websock *result = calloc(1, sizeof *result);
	if (result) {
		result->itf = itf;
		result->closure = closure;
		result->maxlength = 65000;
	}
	return result;
}

void websock_destroy(struct websock *ws)
{
	free(ws);
}
