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
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include "afb-wsj1.h"

/**************** WebSocket handshake ****************************/

static const char *compkeys[32] = {
	"lYKr2sn9+ILcLpkqdrE2VQ==", "G5J7ncQnmS/MubIYcqKWM+E6k8I=",
	"gjN6eOU/6Yy7dBTJ+EaQSw==", "P5QzN7mRt4DeRWxKdG7s4/NCEwk=",
	"ziLin6OQ0/a1+cGaI9Mupg==", "yvpxcFJAGam6huL77vz34CdShyU=",
	"KMfd2bHKah0U5mk2Kg/LIg==", "lyYxfDP5YunhkBF+nAWb/w6K4yg=",
	"fQ/ISF1mNCPRMyAj3ucqNg==", "91YY1EUelb4eMU24Z8WHhJ9cHmc=",
	"RHlfiVVE1lM1AJnErI8dFg==", "UdZQc0JaihQJV5ETCZ84Av88pxQ=",
	"NVy3L2ujXN7v3KEJwK92ww==", "+dE7iITxhExjBtf06VYNWChHqx8=",
	"cCNAgttlgELfbDDIfhujww==", "W2JiswqbTAXx5u84EtjbtqAW2Bg=",
	"K+oQvEDWJP+WXzRS5BJDFw==", "szgW10a9AuD+HtfS4ylaqWfzWAs=",
	"nmg43S4DpVaxye+oQv9KTw==", "8XK74jB9xFfTzzl0wTqW04k3tPE=",
	"LIqZ23sEppbF4YJR9LQ4/w==", "f8lJBQEbR8QmmvPHZpA0smlIeeA=",
	"WY1vvvY2j/3V9DAGW3ZZcA==", "lROlE4vL4cjU1Vnk6rISc9gVKN0=",
	"Ia+dgHnA9QaBrbxuqh4wgQ==", "GiGjxFdSaF0EGTl2cjvFsVmJnfM=",
	"MfpIVG082jFTV7SxTNNijQ==", "f5I2h53hBsT5ES3EHhnxAJ2nqsw=",
	"kFumnAw5d/WctG0yAUHPiQ==", "aQQmOjoABl7mrbliTPS1bOkndOs=",
	"MHiEc+Qc8w/SJ3zMHEM8pA==", "FVCxLBmoil3gY0jSX3aNJ6kR/t4="
};

static const char websocket_s[] = "websocket";
static const char sec_websocket_key_s[] = "Sec-WebSocket-Key";
static const char sec_websocket_version_s[] = "Sec-WebSocket-Version";
static const char sec_websocket_accept_s[] = "Sec-WebSocket-Accept";
static const char sec_websocket_protocol_s[] = "Sec-WebSocket-Protocol";

static const char vseparators[] = " \t,";

/* get randomly a pair of key/accept value */
static void getkeypair(const char **key, const char **ack)
{
	int r;
	r = rand();
	while (r > 15)
		r = (r & 15) + (r >> 4);
	r = (r & 15) << 1;
	*key = compkeys[r];
	*ack = compkeys[r+1];
}

/* joins the strings using the separator */
static char *strjoin(int count, const char **strings, const char *separ)
{
	char *result, *iter;
	size_t length;
	int idx;

	/* creates the count if needed */
	if (count < 0)
		for(count = 0 ; strings[count] != NULL ; count++);

	/* compute the length of the result */
	if (count == 0)
		length = 0;
	else {
		length = (unsigned)(count - 1) * strlen(separ);
		for (idx = 0 ; idx < count ; idx ++)
			length += strlen(strings[idx]);
	}

	/* allocates the result */
	result = malloc(length + 1);
	if (result == NULL)
		errno = ENOMEM;
	else {
		/* create the result */
		if (count != 0) {
			iter = stpcpy(result, strings[idx = 0]);
			while (++idx < count)
				iter = stpcpy(stpcpy(iter, separ), strings[idx]);
			// assert(iter - result == length);
		}
		result[length] = 0;
	}
	return result;
}

/* creates the http message for the request */
static int make_request(char **request, const char *path, const char *host, const char *key, const char *protocols)
{
	int rc = asprintf(request, 
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Version: 13\r\n"
			"Sec-WebSocket-Key: %s\r\n"
			"Sec-WebSocket-Protocol: %s\r\n"
			"Content-Length: 0\r\n"
			"\r\n"
			, path
			, host
			, key
			, protocols
		);
	if (rc < 0) {
		errno = ENOMEM;
		*request = NULL;
		return -1;
	}
	return rc;
}

/* create the request and send it to fd, returns the expected accept string */
static const char *send_request(int fd, const char **protocols, const char *path, const char *host)
{
	const char *key, *ack;
	char *protolist, *request;
	int length, rc;

	/* make the list of accepted protocols */
	protolist = strjoin(-1, protocols, ", ");
	if (protolist == NULL)
		return NULL;

	/* create the request */
	getkeypair(&key, &ack);
	length = make_request(&request, path, host, key, protolist);
	free(protolist);
	if (length < 0)
		return NULL;

	/* send the request */
	do { rc = (int)write(fd, request, length); } while(rc < 0 && errno == EINTR);
	free(request);
	return rc < 0 ? NULL : ack;
}

/* read a line not efficiently but without buffering */
static int receive_line(int fd, char *line, int size)
{
	int rc, length = 0, cr = 0;
	for(;;) {
		if (length >= size) {
			errno = EFBIG;
			return -1;
		}
		do { rc = (int)read(fd, line + length, 1); } while (rc < 0 && errno == EINTR);
		if (rc < 0)
			return -1;
		if (line[length] == '\r')
			cr = 1;
		else if (cr != 0 && line[length] == '\n') {
			line[--length] = 0;
			return length;
		} else
			cr = 0;
		length++;
	}
}

/* check a header */
static inline int isheader(const char *head, size_t klen, const char *key)
{
	return strncasecmp(head, key, klen) == 0 && key[klen] == 0;
}

/* receives and scan the response */
static int receive_response(int fd, const char **protocols, const char *ack)
{
	char line[4096], *it;
	int rc, haserr, result = -1;
	size_t len, clen;

	/* check the header line to be something like: "HTTP/1.1 101 Switching Protocols" */
	rc = receive_line(fd, line, (int)sizeof(line));
	if (rc < 0)
		goto error;
	len = strcspn(line, " ");
	if (len != 8 || 0 != strncmp(line, "HTTP/1.1", 8))
		goto abort;
	it = line + len;
	len = strspn(it, " ");
	if (len == 0)
		goto abort;
	it += len;
	len = strcspn(it, " ");
	if (len != 3 || 0 != strncmp(it, "101", 3))
		goto abort;

	/* reads the rest of the response until empty line */
	clen = 0;
	haserr = 0;
	for(;;) {
		rc = receive_line(fd, line, (int)sizeof(line));
		if (rc < 0)
			goto error;
		if (rc == 0)
			break;
		len = strcspn(line, ": ");
		if (len != 0 && line[len] == ':') {
			/* checks the headers values */
			it = line + len + 1;
			it += strspn(it, " ,");
			it[strcspn(it, " ,")] = 0;
			if (isheader(line, len, "Sec-WebSocket-Accept")) {
				if (strcmp(it, ack) != 0)
					haserr = 1;
			} else if (isheader(line, len, "Sec-WebSocket-Protocol")) {
				result = 0;
				while(protocols[result] != NULL && strcmp(it, protocols[result]) != 0)
					result++;
			} else if (isheader(line, len, "Upgrade")) {
				if (strcmp(it, "websocket") != 0)
					haserr = 1;
			} else if (isheader(line, len, "Content-Length")) {
				clen = atol(it);
			}
		}
	}

	/* skips the remaining of the message */
	while (clen >= sizeof line) {
		while (read(fd, line, sizeof line) < 0 && errno == EINTR);
		clen -= sizeof line;
	}
	if (clen > 0) {
		while (read(fd, line, len) < 0 && errno == EINTR);
	}
	if (haserr != 0 || result < 0)
		goto abort;
	return result;
abort:
	errno = ECONNABORTED;
error:
	return -1;
}

static int negociate(int fd, const char **protocols, const char *path, const char *host)
{
	const char *ack = send_request(fd, protocols, path, host);
	return ack == NULL ? -1 : receive_response(fd, protocols, ack);
}

/* tiny parse a "standard" websock uri ws://host:port/path... */
static int parse_uri(const char *uri, char **host, char **service, const char **path)
{
	const char *h, *p;
	size_t hlen, plen;

	/* the scheme */
	if (strncmp(uri, "ws://", 5) == 0)
		uri += 5;

	/* the host */
	h = uri;
	hlen = strcspn(h, ":/");
	if (hlen == 0)
		goto invalid;
	uri += hlen;

	/* the port (optional) */
	if (*uri == ':') {
		p = ++uri;
		plen = strcspn(p, "/");
		if (plen == 0)
			goto invalid;
		uri += plen;
	} else {
		p = NULL;
		plen = 0;
	}

	/* the path */
	if (*uri != '/')
		goto invalid;

	/* make the result */
	*host = strndup(h, hlen);
	if (*host != NULL) {
		*service = plen ? strndup(p, plen) : strdup("http");
		if (*service != NULL) {
			*path = uri;
			return 0;
		}
		free(*host);
	}
	errno = ENOMEM;
	goto error;
invalid:
	errno = EINVAL;
error:
	return -1;
	
}




static const char *proto_json1[2] = { "x-afb-ws-json1",	NULL };

struct afb_wsj1 *afb_ws_client_connect_wsj1(const char *uri, struct afb_wsj1_itf *itf, void *closure)
{
	int rc, fd;
	char *host, *service, xhost[32];
	const char *path;
	struct addrinfo hint, *rai, *iai;
	struct afb_wsj1 *result;

	/* scan the uri */
	rc = parse_uri(uri, &host, &service, &path);
	if (rc < 0)
		return NULL;

	/* get addr */
	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_INET;
	hint.ai_socktype = SOCK_STREAM;
	rc = getaddrinfo(host, service, &hint, &rai);
	free(host);
	free(service);
	if (rc != 0) {
		errno = EINVAL;
		return NULL;
	}

	/* get the socket */
	result = NULL;
	iai = rai;
	while (iai != NULL) {
		struct sockaddr_in *a = (struct sockaddr_in*)(iai->ai_addr);
		unsigned char *ipv4 = (unsigned char*)&(a->sin_addr.s_addr);
		unsigned char *port = (unsigned char*)&(a->sin_port);
		sprintf(xhost, "%d.%d.%d.%d:%d",
			(int)ipv4[0], (int)ipv4[1], (int)ipv4[2], (int)ipv4[3],
			(((int)port[0]) << 8)|(int)port[1]);
		fd = socket(iai->ai_family, iai->ai_socktype, iai->ai_protocol);
		if (fd >= 0) {
			rc = connect(fd, iai->ai_addr, iai->ai_addrlen);
			if (rc == 0) {
				rc = negociate(fd, proto_json1, path, xhost);
				if (rc == 0) {
					result = afb_wsj1_create(fd, itf, closure);
					if (result != NULL) {
						fcntl(fd, F_SETFL, O_NONBLOCK);
						break;
					}
				}
			}
			close(fd);
		}
		iai = iai->ai_next;
	}
	freeaddrinfo(rai);
	return result;
}

#if 0
/* compute the queried path */
static char *makequery(const char *path, const char *uuid, const char *token)
{
	char *result;
	int rc;

	while(*path == '/')
		path++;
	if (uuid == NULL) {
		if (token == NULL)
			rc = asprintf(&result, "/%s", path);
		else
			rc = asprintf(&result, "/%s?x-afb-token=%s", path, token);
	} else {
		if (token == NULL)
			rc = asprintf(&result, "/%s?x-afb-uuid=%s", path, uuid);
		else
			rc = asprintf(&result, "/%s?x-afb-uuid=%s&x-afb-token=%s", path, uuid, token);
	}
	if (rc < 0) {
		errno = ENOMEM;
		return NULL;
	}
	return result;
}
#endif


