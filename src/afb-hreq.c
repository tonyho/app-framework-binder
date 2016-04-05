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
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <microhttpd.h>

#include "local-def.h"
#include "afb-method.h"
#include "afb-req-itf.h"
#include "afb-hreq.h"
#include "session.h"
#include "verbose.h"

#define SIZE_RESPONSE_BUFFER   8000

static char empty_string[] = "";

static const char uuid_header[] = "x-afb-uuid";
static const char uuid_arg[] = "uuid";
static const char uuid_cookie[] = "uuid";

static const char token_header[] = "x-afb-token";
static const char token_arg[] = "token";
static const char token_cookie[] = "token";


struct hreq_data {
	struct hreq_data *next;
	char *key;
	int file;
	size_t length;
	char *value;
};

static struct afb_arg req_get(struct afb_hreq *hreq, const char *name);
static void req_iterate(struct afb_hreq *hreq, int (*iterator)(void *closure, struct afb_arg arg), void *closure);
static void req_fail(struct afb_hreq *hreq, const char *status, const char *info);
static void req_success(struct afb_hreq *hreq, json_object *obj, const char *info);
static int req_session_create(struct afb_hreq *hreq);
static int req_session_check(struct afb_hreq *hreq, int refresh);
static void req_session_close(struct afb_hreq *hreq);

static const struct afb_req_itf afb_hreq_itf = {
	.get = (void*)req_get,
	.iterate = (void*)req_iterate,
	.fail = (void*)req_fail,
	.success = (void*)req_success,
	.session_create = (void*)req_session_create,
	.session_check = (void*)req_session_check,
	.session_close = (void*)req_session_close
};

static struct hreq_data *get_data(struct afb_hreq *hreq, const char *key, int create)
{
	struct hreq_data *data = hreq->data;
	if (key == NULL)
		key = empty_string;
	while (data != NULL) {
		if (!strcasecmp(data->key, key))
			return data;
		data = data->next;
	}
	if (create) {
		data = calloc(1, sizeof *data);
		if (data != NULL) {
			data->key = strdup(key);
			if (data->key == NULL) {
				free(data);
				data = NULL;
			} else {
				data->next = hreq->data;
				hreq->data = data;
			}
		}
	}
	return data;
}

/* a valid subpath is a relative path not looking deeper than root using .. */
static int validsubpath(const char *subpath)
{
	int l = 0, i = 0;

	while (subpath[i]) {
		switch (subpath[i++]) {
		case '.':
			if (!subpath[i])
				break;
			if (subpath[i] == '/') {
				i++;
				break;
			}
			if (subpath[i++] == '.') {
				if (!subpath[i]) {
					if (--l < 0)
						return 0;
					break;
				}
				if (subpath[i++] == '/') {
					if (--l < 0)
						return 0;
					break;
				}
			}
		default:
			while (subpath[i] && subpath[i] != '/')
				i++;
			l++;
		case '/':
			break;
		}
	}
	return 1;
}

void afb_hreq_free(struct afb_hreq *hreq)
{
	struct hreq_data *data;
	if (hreq != NULL) {
		if (hreq->postform != NULL)
			MHD_destroy_post_processor(hreq->postform);
		for (data = hreq->data; data; data = hreq->data) {
			hreq->data = data->next;
			free(data->key);
			free(data->value);
			free(data);
		}
		ctxClientPut(hreq->context);
		free(hreq);
	}
}

/*
 * Removes the 'prefix' of 'length' from the tail of 'hreq'
 * if and only if the prefix exists and is terminated by a leading
 * slash
 */
int afb_hreq_unprefix(struct afb_hreq *hreq, const char *prefix, size_t length)
{
	/* check the prefix ? */
	if (length > hreq->lentail || (hreq->tail[length] && hreq->tail[length] != '/')
	    || strncasecmp(prefix, hreq->tail, length))
		return 0;

	/* removes successives / */
	while (length < hreq->lentail && hreq->tail[length + 1] == '/')
		length++;

	/* update the tail */
	hreq->lentail -= length;
	hreq->tail += length;
	return 1;
}

int afb_hreq_valid_tail(struct afb_hreq *hreq)
{
	return validsubpath(hreq->tail);
}

void afb_hreq_reply_error(struct afb_hreq *hreq, unsigned int status)
{
	char *buffer;
	int length;
	struct MHD_Response *response;

	length = asprintf(&buffer, "<html><body>error %u</body></html>", status);
	if (length > 0)
		response = MHD_create_response_from_buffer((unsigned)length, buffer, MHD_RESPMEM_MUST_FREE);
	else {
		buffer = "<html><body>error</body></html>";
		response = MHD_create_response_from_buffer(strlen(buffer), buffer, MHD_RESPMEM_PERSISTENT);
	}
	if (!MHD_queue_response(hreq->connection, status, response))
		fprintf(stderr, "Failed to reply error code %u", status);
	MHD_destroy_response(response);
}

int afb_hreq_reply_file_if_exist(struct afb_hreq *hreq, int dirfd, const char *filename)
{
	int rc;
	int fd;
	unsigned int status;
	struct stat st;
	char etag[1 + 2 * sizeof(int)];
	const char *inm;
	struct MHD_Response *response;

	/* Opens the file or directory */
	if (filename[0]) {
		fd = openat(dirfd, filename, O_RDONLY);
		if (fd < 0) {
			if (errno == ENOENT)
				return 0;
			afb_hreq_reply_error(hreq, MHD_HTTP_FORBIDDEN);
			return 1;
		}
	} else {
		fd = dup(dirfd);
		if (fd < 0) {
			afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
			return 1;
		}
	}

	/* Retrieves file's status */
	if (fstat(fd, &st) != 0) {
		close(fd);
		afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
		return 1;
	}

	/* serve directory */
	if (S_ISDIR(st.st_mode)) {
		if (hreq->url[hreq->lenurl - 1] != '/') {
			/* the redirect is needed for reliability of relative path */
			char *tourl = alloca(hreq->lenurl + 2);
			memcpy(tourl, hreq->url, hreq->lenurl);
			tourl[hreq->lenurl] = '/';
			tourl[hreq->lenurl + 1] = 0;
			rc = afb_hreq_redirect_to(hreq, tourl);
		} else {
			rc = afb_hreq_reply_file_if_exist(hreq, fd, "index.html");
		}
		close(fd);
		return rc;
	}

	/* Don't serve special files */
	if (!S_ISREG(st.st_mode)) {
		close(fd);
		afb_hreq_reply_error(hreq, MHD_HTTP_FORBIDDEN);
		return 1;
	}

	/* Check the method */
	if ((hreq->method & (afb_method_get | afb_method_head)) == 0) {
		close(fd);
		afb_hreq_reply_error(hreq, MHD_HTTP_METHOD_NOT_ALLOWED);
		return 1;
	}

	/* computes the etag */
	sprintf(etag, "%08X%08X", ((int)(st.st_mtim.tv_sec) ^ (int)(st.st_mtim.tv_nsec)), (int)(st.st_size));

	/* checks the etag */
	inm = MHD_lookup_connection_value(hreq->connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_NONE_MATCH);
	if (inm && 0 == strcmp(inm, etag)) {
		/* etag ok, return NOT MODIFIED */
		close(fd);
		if (verbosity)
			fprintf(stderr, "Not Modified: [%s]\n", filename);
		response = MHD_create_response_from_buffer(0, empty_string, MHD_RESPMEM_PERSISTENT);
		status = MHD_HTTP_NOT_MODIFIED;
	} else {
		/* check the size */
		if (st.st_size != (off_t) (size_t) st.st_size) {
			close(fd);
			afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
			return 1;
		}

		/* create the response */
		response = MHD_create_response_from_fd((size_t) st.st_size, fd);
		status = MHD_HTTP_OK;

#if defined(USE_MAGIC_MIME_TYPE)
		/* set the type */
		if (hreq->session->magic) {
			const char *mimetype = magic_descriptor(hreq->session->magic, fd);
			if (mimetype != NULL)
				MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, mimetype);
		}
#endif
	}

	/* fills the value and send */
	MHD_add_response_header(response, MHD_HTTP_HEADER_CACHE_CONTROL, hreq->session->cacheTimeout);
	MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, etag);
	MHD_queue_response(hreq->connection, status, response);
	MHD_destroy_response(response);
	return 1;
}

int afb_hreq_reply_file(struct afb_hreq *hreq, int dirfd, const char *filename)
{
	int rc = afb_hreq_reply_file_if_exist(hreq, dirfd, filename);
	if (rc == 0)
		afb_hreq_reply_error(hreq, MHD_HTTP_NOT_FOUND);
	return 1;
}

int afb_hreq_redirect_to(struct afb_hreq *hreq, const char *url)
{
	struct MHD_Response *response;

	response = MHD_create_response_from_buffer(0, empty_string, MHD_RESPMEM_PERSISTENT);
	MHD_add_response_header(response, MHD_HTTP_HEADER_LOCATION, url);
	MHD_queue_response(hreq->connection, MHD_HTTP_MOVED_PERMANENTLY, response);
	MHD_destroy_response(response);
	if (verbosity)
		fprintf(stderr, "redirect from [%s] to [%s]\n", hreq->url, url);
	return 1;
}

const char *afb_hreq_get_cookie(struct afb_hreq *hreq, const char *name)
{
	return MHD_lookup_connection_value(hreq->connection, MHD_COOKIE_KIND, name);
}

const char *afb_hreq_get_argument(struct afb_hreq *hreq, const char *name)
{
	struct hreq_data *data = get_data(hreq, name, 0);
	return data ? data->value : MHD_lookup_connection_value(hreq->connection, MHD_GET_ARGUMENT_KIND, name);
}

const char *afb_hreq_get_header(struct afb_hreq *hreq, const char *name)
{
	return MHD_lookup_connection_value(hreq->connection, MHD_HEADER_KIND, name);
}

void afb_hreq_post_end(struct afb_hreq *hreq)
{
	struct hreq_data *data = hreq->data;
	while(data) {
		if (data->file > 0) {
			close(data->file);
			data->file = -1;
		}
		data = data->next;
	}
}

int afb_hreq_post_add(struct afb_hreq *hreq, const char *key, const char *data, size_t size)
{
	void *p;
	struct hreq_data *hdat = get_data(hreq, key, 1);
	if (hdat->file) {
		return 0;
	}
	p = realloc(hdat->value, hdat->length + size + 1);
	if (p == NULL) {
		return 0;
	}
	hdat->value = p;
	memcpy(&hdat->value[hdat->length], data, size);
	hdat->length += size;
	hdat->value[hdat->length] = 0;
	return 1;
}

int afb_hreq_post_add_file(struct afb_hreq *hreq, const char *key, const char *file, const char *data, size_t size)
{
	struct hreq_data *hdat = get_data(hreq, key, 1);

	/* continuation with reopening */
	if (hdat->file < 0) {
		hdat->file = open(hdat->value, O_WRONLY|O_APPEND);
		if (hdat->file == 0) {
			hdat->file = dup(0);
			close(0);
		}
		if (hdat->file <= 0)
			return 0;
	}
	if (hdat->file > 0) {
		write(hdat->file, data, size);
		return 1;
	}

	/* creation */
	/* TODO */
	return 0;
	
}

int afb_hreq_is_argument_a_file(struct afb_hreq *hreq, const char *key)
{
	struct hreq_data *hdat = get_data(hreq, key, 0);
	return hdat != NULL && hdat->file != 0;
}


struct afb_req afb_hreq_to_req(struct afb_hreq *hreq)
{
	return (struct afb_req){ .itf = &afb_hreq_itf, .data = hreq };
}

static struct afb_arg req_get(struct afb_hreq *hreq, const char *name)
{
	struct hreq_data *hdat = get_data(hreq, name, 0);
	if (hdat)
		return (struct afb_arg){
			.name = hdat->key,
			.value = hdat->value,
			.size = hdat->length,
			.is_file = (hdat->file != 0)
		};
		
	return (struct afb_arg){
		.name = name,
		.value = MHD_lookup_connection_value(hreq->connection, MHD_GET_ARGUMENT_KIND, name),
		.size = 0,
		.is_file = 0
	};
}

struct iterdata
{
	struct afb_hreq *hreq;
	int (*iterator)(void *closure, struct afb_arg arg);
	void *closure;
};

static int _iterargs_(struct iterdata *id, enum MHD_ValueKind kind, const char *key, const char *value)
{
	if (get_data(id->hreq, key, 0))
		return 1;
	return id->iterator(id->closure, (struct afb_arg){
		.name = key,
		.value = value,
		.size = 0,
		.is_file = 0
	});
}

static void req_iterate(struct afb_hreq *hreq, int (*iterator)(void *closure, struct afb_arg arg), void *closure)
{
	struct iterdata id = { .hreq = hreq, .iterator = iterator, .closure = closure };
	struct hreq_data *hdat = hreq->data;
	while (hdat) {
		if (!iterator(closure, (struct afb_arg){
			.name = hdat->key,
			.value = hdat->value,
			.size = hdat->length,
			.is_file = (hdat->file != 0)}))
			return;
		hdat = hdat->next;
	}
	MHD_get_connection_values (hreq->connection, MHD_GET_ARGUMENT_KIND, (void*)_iterargs_, &id);
}

static ssize_t send_json_cb(json_object *obj, uint64_t pos, char *buf, size_t max)
{
	ssize_t len = stpncpy(buf, json_object_to_json_string(obj)+pos, max) - buf;
	return len ? : -1;
}

static void req_reply(struct afb_hreq *hreq, unsigned retcode, const char *status, const char *info, json_object *resp)
{
	json_object *root, *request;
	struct MHD_Response *response;

	root = json_object_new_object();
	json_object_object_add(root, "jtype", json_object_new_string("afb-reply"));
	request = json_object_new_object();
	json_object_object_add(root, "request", request);
	json_object_object_add(request, "status", json_object_new_string(status));
	if (info)
		json_object_object_add(request, "info", json_object_new_string(info));
	if (resp)
		json_object_object_add(root, "response", resp);
	if (hreq->context) {
		json_object_object_add(request, uuid_arg, json_object_new_string(hreq->context->uuid));
		json_object_object_add(request, token_arg, json_object_new_string(hreq->context->token));
	}

	response = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, SIZE_RESPONSE_BUFFER, (void*)send_json_cb, root, (void*)json_object_put);
	MHD_queue_response(hreq->connection, retcode, response);
	MHD_destroy_response(response);
}

static void req_fail(struct afb_hreq *hreq, const char *status, const char *info)
{
	req_reply(hreq, MHD_HTTP_OK, status, info, NULL);
}

static void req_success(struct afb_hreq *hreq, json_object *obj, const char *info)
{
	req_reply(hreq, MHD_HTTP_OK, "success", info, obj);
}

struct AFB_clientCtx *afb_hreq_context(struct afb_hreq *hreq)
{
	const char *uuid;

	if (hreq->context == NULL) {
		uuid = afb_hreq_get_header(hreq, uuid_header);
		if (uuid == NULL)
			uuid = afb_hreq_get_argument(hreq, uuid_arg);
		if (uuid == NULL)
			uuid = afb_hreq_get_cookie(hreq, uuid_cookie);
		hreq->context = ctxClientGet(uuid);
	}
	return hreq->context;
}

static int req_session_create(struct afb_hreq *hreq)
{
	struct AFB_clientCtx *context = afb_hreq_context(hreq);
	if (context == NULL)
		return 0;
	if (context->created)
		return 0;
	return req_session_check(hreq, 1);
}

static int req_session_check(struct afb_hreq *hreq, int refresh)
{
	const char *token;

	struct AFB_clientCtx *context = afb_hreq_context(hreq);

	if (context == NULL)
		return 0;

	token = afb_hreq_get_header(hreq, token_header);
	if (token == NULL)
		token = afb_hreq_get_argument(hreq, token_arg);
	if (token == NULL)
		token = afb_hreq_get_cookie(hreq, token_cookie);
	if (token == NULL)
		return 0;

	if (!ctxTokenCheck (context, token))
		return 0;

	if (refresh) {
		ctxTokenNew (context);
	}

	return 1;
}

static void req_session_close(struct afb_hreq *hreq)
{
	struct AFB_clientCtx *context = afb_hreq_context(hreq);
	if (context != NULL)
		ctxClientClose(context);
}



