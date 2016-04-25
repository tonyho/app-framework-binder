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
#include <json.h>

#if defined(USE_MAGIC_MIME_TYPE)
#include <magic.h>
#endif

#include "afb-method.h"
#include "afb-req-itf.h"
#include "afb-msg-json.h"
#include "afb-hreq.h"
#include "session.h"
#include "verbose.h"

#define SIZE_RESPONSE_BUFFER   8192

static char empty_string[] = "";

static const char uuid_header[] = "x-afb-uuid";
static const char uuid_arg[] = "uuid";
static const char uuid_cookie[] = "uuid";

static const char token_header[] = "x-afb-token";
static const char token_arg[] = "token";
static const char token_cookie[] = "token";

static char *cookie_name = NULL;
static char *cookie_setter = NULL;
static char *tmp_pattern = NULL;

struct hreq_data {
	struct hreq_data *next;
	char *key;
	size_t length;
	char *value;
	char *path;
};

static struct json_object *req_json(struct afb_hreq *hreq);
static struct afb_arg req_get(struct afb_hreq *hreq, const char *name);
static void req_fail(struct afb_hreq *hreq, const char *status, const char *info);
static void req_success(struct afb_hreq *hreq, json_object *obj, const char *info);
static const char *req_raw(struct afb_hreq *hreq, size_t *size);
static void req_send(struct afb_hreq *hreq, char *buffer, size_t size);
static int req_session_create(struct afb_hreq *hreq);
static int req_session_check(struct afb_hreq *hreq, int refresh);
static void req_session_close(struct afb_hreq *hreq);

static const struct afb_req_itf afb_hreq_itf = {
	.json = (void*)req_json,
	.get = (void*)req_get,
	.success = (void*)req_success,
	.fail = (void*)req_fail,
	.raw = (void*)req_raw,
	.send = (void*)req_send,
	.session_create = (void*)req_session_create,
	.session_check = (void*)req_session_check,
	.session_close = (void*)req_session_close,
	.context_get = (void*)afb_context_get,
	.context_set = (void*)afb_context_set
};

static struct hreq_data *get_data(struct afb_hreq *hreq, const char *key, int create)
{
	struct hreq_data *data = hreq->data;
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

static void afb_hreq_reply_v(struct afb_hreq *hreq, unsigned status, struct MHD_Response *response, va_list args)
{
	char *cookie;
	const char *k, *v;
	k = va_arg(args, const char *);
	while (k != NULL) {
		v = va_arg(args, const char *);
		MHD_add_response_header(response, k, v);
		k = va_arg(args, const char *);
	}
	if (hreq->context != NULL && asprintf(&cookie, cookie_setter, hreq->context->uuid)) {
		MHD_add_response_header(response, MHD_HTTP_HEADER_SET_COOKIE, cookie);
		free(cookie);
	}
	MHD_queue_response(hreq->connection, status, response);
	MHD_destroy_response(response);
}

void afb_hreq_reply(struct afb_hreq *hreq, unsigned status, struct MHD_Response *response, ...)
{
	va_list args;
	va_start(args, response);
	afb_hreq_reply_v(hreq, status, response, args);
	va_end(args);
}

void afb_hreq_reply_empty(struct afb_hreq *hreq, unsigned status, ...)
{
	va_list args;
	va_start(args, status);
	afb_hreq_reply_v(hreq, status, MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT), args);
	va_end(args);
}

void afb_hreq_reply_static(struct afb_hreq *hreq, unsigned status, size_t size, char *buffer, ...)
{
	va_list args;
	va_start(args, buffer);
	afb_hreq_reply_v(hreq, status, MHD_create_response_from_buffer((unsigned)size, buffer, MHD_RESPMEM_PERSISTENT), args);
	va_end(args);
}

void afb_hreq_reply_copy(struct afb_hreq *hreq, unsigned status, size_t size, char *buffer, ...)
{
	va_list args;
	va_start(args, buffer);
	afb_hreq_reply_v(hreq, status, MHD_create_response_from_buffer((unsigned)size, buffer, MHD_RESPMEM_MUST_COPY), args);
	va_end(args);
}

void afb_hreq_reply_free(struct afb_hreq *hreq, unsigned status, size_t size, char *buffer, ...)
{
	va_list args;
	va_start(args, buffer);
	afb_hreq_reply_v(hreq, status, MHD_create_response_from_buffer((unsigned)size, buffer, MHD_RESPMEM_MUST_FREE), args);
	va_end(args);
}

#if defined(USE_MAGIC_MIME_TYPE)

#if !defined(MAGIC_DB)
#define MAGIC_DB "/usr/share/misc/magic.mgc"
#endif

static magic_t lazy_libmagic()
{
	static int done = 0;
	static magic_t result = NULL;

	if (!done) {
		done = 1;
		/* MAGIC_MIME tells magic to return a mime of the file,
			 but you can specify different things */
		if (verbosity)
			fprintf(stderr, "Loading mimetype default magic database\n");

		result = magic_open(MAGIC_MIME_TYPE);
		if (result == NULL) {
			fprintf(stderr,"ERROR: unable to initialize magic library\n");
		}
		/* Warning: should not use NULL for DB
				[libmagic bug wont pass efence check] */
		else if (magic_load(result, MAGIC_DB) != 0) {
			fprintf(stderr,"cannot load magic database - %s\n",
					magic_error(result));
			magic_close(result);
			result = NULL;
		}
	}

	return result;
}

static const char *magic_mimetype_fd(int fd)
{
	magic_t lib = lazy_libmagic();
	return lib ? magic_descriptor(lib, fd) : NULL;
}

#endif

static const char *mimetype_fd_name(int fd, const char *filename)
{
	const char *result = NULL;

#if defined(INFER_EXTENSION)
	const char *extension = strrchr(filename, '.');
	if (extension) {
		static const char *const known[][2] = {
			{ ".js",   "text/javascript" },
			{ ".html", "text/html" },
			{ ".css",  "text/css" },
			{ NULL, NULL }
		};
		int i = 0;
		while (known[i][0]) {
			if (!strcasecmp(extension, known[i][0])) {
				result = known[i][1];
				break;
			}
			i++;
		}
	}
#endif
#if defined(USE_MAGIC_MIME_TYPE)
	if (result == NULL)
		result = magic_mimetype_fd(fd);
#endif
	return result;
}

void afb_hreq_free(struct afb_hreq *hreq)
{
	struct hreq_data *data;
	if (hreq != NULL) {
		if (hreq->postform != NULL)
			MHD_destroy_post_processor(hreq->postform);
		for (data = hreq->data; data; data = hreq->data) {
			hreq->data = data->next;
			if (data->path) {
				unlink(data->path);
				free(data->path);
			}
			free(data->key);
			free(data->value);
			free(data);
		}
		ctxClientPut(hreq->context);
		json_object_put(hreq->json);
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
	afb_hreq_reply_empty(hreq, status, NULL);
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
	const char *mimetype;

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
		static const char *indexes[] = { "index.html", NULL };
		int i = 0;
		rc = 0;
		while (indexes[i] != NULL) {
			if (faccessat(fd, indexes[i], R_OK, 0) == 0) {
				if (hreq->url[hreq->lenurl - 1] != '/') {
					/* the redirect is needed for reliability of relative path */
					char *tourl = alloca(hreq->lenurl + 2);
					memcpy(tourl, hreq->url, hreq->lenurl);
					tourl[hreq->lenurl] = '/';
					tourl[hreq->lenurl + 1] = 0;
					rc = afb_hreq_redirect_to(hreq, tourl);
				} else {
					rc = afb_hreq_reply_file_if_exist(hreq, fd, indexes[i]);
				}
				break;
			}
			i++;
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

		/* set the type */
		mimetype = mimetype_fd_name(fd, filename);
		if (mimetype != NULL)
			MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, mimetype);
	}

	/* fills the value and send */
	afb_hreq_reply(hreq, status, response,
			MHD_HTTP_HEADER_CACHE_CONTROL, hreq->cacheTimeout,
			MHD_HTTP_HEADER_ETAG, etag,
			NULL);
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
	afb_hreq_reply_static(hreq, MHD_HTTP_MOVED_PERMANENTLY, 0, NULL,
			MHD_HTTP_HEADER_LOCATION, url, NULL);
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

int afb_hreq_post_add(struct afb_hreq *hreq, const char *key, const char *data, size_t size)
{
	void *p;
	struct hreq_data *hdat = get_data(hreq, key, 1);
	if (hdat->path != NULL) {
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

int afb_hreq_init_download_path(const char *directory)
{
	struct stat st;
	size_t n;
	char *p;

	if (access(directory, R_OK|W_OK)) {
		/* no read/write access */
		return -1;
	}
	if (stat(directory, &st)) {
		/* can't get info */
		return -1;
	}
	if (!S_ISDIR(st.st_mode)) {
		/* not a directory */
		errno = ENOTDIR;
		return -1;
	}
	n = strlen(directory);
	while(n > 1 && directory[n-1] == '/') n--;
	p = malloc(n + 8);
	if (p == NULL) {
		/* can't allocate memory */
		errno = ENOMEM;
		return -1;
	}
	memcpy(p, directory, n);
	p[n++] = '/';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n] = 0;
	free(tmp_pattern);
	tmp_pattern = p;
	return 0;
}

static int opentempfile(char **path)
{
	int fd;
	char *fname;

	fname = strdup(tmp_pattern ? : "XXXXXX"); /* TODO improve the path */
	if (fname == NULL)
		return -1;

	fd = mkostemp(fname, O_CLOEXEC|O_WRONLY);
	if (fd < 0)
		free(fname);
	else
		*path = fname;
	return fd;
}

int afb_hreq_post_add_file(struct afb_hreq *hreq, const char *key, const char *file, const char *data, size_t size)
{
	int fd;
	ssize_t sz;
	struct hreq_data *hdat = get_data(hreq, key, 1);

	if (hdat->value == NULL) {
		hdat->value = strdup(file);
		if (hdat->value == NULL)
			return 0;
		fd = opentempfile(&hdat->path);
	} else if (strcmp(hdat->value, file) || hdat->path == NULL) {
		return 0;
	} else {
		fd = open(hdat->path, O_WRONLY|O_APPEND);
	}
	if (fd < 0)
		return 0;
	while (size) {
		sz = write(fd, data, size);
		if (sz >= 0) {
			hdat->length += (size_t)sz;
			size -= (size_t)sz;
			data += sz;
		} else if (errno != EINTR)
			break;
	}
	close(fd);
	return !size;
}

struct afb_req afb_hreq_to_req(struct afb_hreq *hreq)
{
	return (struct afb_req){ .itf = &afb_hreq_itf, .req_closure = hreq };
}

static struct afb_arg req_get(struct afb_hreq *hreq, const char *name)
{
	struct hreq_data *hdat = get_data(hreq, name, 0);
	if (hdat)
		return (struct afb_arg){
			.name = hdat->key,
			.value = hdat->value,
			.path = hdat->path
		};
		
	return (struct afb_arg){
		.name = name,
		.value = MHD_lookup_connection_value(hreq->connection, MHD_GET_ARGUMENT_KIND, name),
		.path = NULL
	};
}

static int _iterargs_(struct json_object *obj, enum MHD_ValueKind kind, const char *key, const char *value)
{
	json_object_object_add(obj, key, value ? json_object_new_string(value) : NULL);
	return 1;
}

static struct json_object *req_json(struct afb_hreq *hreq)
{
	struct hreq_data *hdat;
	struct json_object *obj, *val;

	obj = hreq->json;
	if (obj == NULL) {
		hreq->json = obj = json_object_new_object();
		if (obj == NULL) {
		} else {
			MHD_get_connection_values (hreq->connection, MHD_GET_ARGUMENT_KIND, (void*)_iterargs_, obj);
			for (hdat = hreq->data ; hdat ; hdat = hdat->next) {
				if (hdat->path == NULL)
					val = hdat->value ? json_object_new_string(hdat->value) : NULL;
				else {
					val = json_object_new_object();
					if (val == NULL) {
					} else {
						json_object_object_add(val, "file", json_object_new_string(hdat->value));
						json_object_object_add(val, "path", json_object_new_string(hdat->path));
					}
				}
				json_object_object_add(obj, hdat->key, val);
			}
		}
	}
	return obj;
}

static const char *req_raw(struct afb_hreq *hreq, size_t *size)
{
	const char *result = json_object_get_string(req_json(hreq));
	*size = result ? strlen(result) : 0;
	return result;
}

static void req_send(struct afb_hreq *hreq, char *buffer, size_t size)
{
	afb_hreq_reply_free(hreq, MHD_HTTP_OK, size, buffer, NULL);
}

static ssize_t send_json_cb(json_object *obj, uint64_t pos, char *buf, size_t max)
{
	ssize_t len = stpncpy(buf, json_object_to_json_string(obj)+pos, max) - buf;
	return len ? : MHD_CONTENT_READER_END_OF_STREAM;
}

static void req_reply(struct afb_hreq *hreq, unsigned retcode, const char *status, const char *info, json_object *resp)
{
	struct json_object *reply;
	const char *token, *uuid;
	struct MHD_Response *response;

	if (hreq->context == NULL) {
		token = uuid = NULL;
	} else {
		token = hreq->context->token;
		uuid = hreq->context->uuid;
	}
	reply = afb_msg_json_reply(status, info, resp, token, uuid);
	response = MHD_create_response_from_callback((uint64_t)strlen(json_object_to_json_string(reply)), SIZE_RESPONSE_BUFFER, (void*)send_json_cb, reply, (void*)json_object_put);
	afb_hreq_reply(hreq, retcode, response, NULL);
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
			uuid = afb_hreq_get_cookie(hreq, cookie_name);
		hreq->context = ctxClientGetForUuid(uuid);
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

int afb_hreq_init_cookie(int port, const char *path, int maxage)
{
	int rc;

	free(cookie_name);
	free(cookie_setter);
	cookie_name = NULL;
	cookie_setter = NULL;

	path = path ? : "/";
	rc = asprintf(&cookie_name, "x-afb-uuid-%d", port);
	if (rc < 0)
		return 0;
	rc = asprintf(&cookie_setter, "%s=%%s; Path=%s; Max-Age=%d; HttpOnly",
			cookie_name, path, maxage);
	if (rc < 0)
		return 0;
	return 1;
}


