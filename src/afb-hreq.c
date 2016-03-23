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
#include <microhttpd.h>
#include <assert.h>
#include <poll.h>
#include <sys/stat.h>

#include "../include/local-def.h"
#include "afb-method.h"
#include "afb-req-itf.h"
#include "afb-hreq.h"

static char empty_string[1] = "";


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

/*
 * Removes the 'prefix' of 'length' frome the tail of 'hreq'
 * if and only if the prefix exists and is terminated by a leading
 * slash
 */
int afb_hreq_unprefix(struct afb_hreq *hreq, const char *prefix, size_t length)
{
	/* check the prefix ? */
	if (length > hreq->lentail || (hreq->tail[length] && hreq->tail[length] != '/')
	    || memcmp(prefix, hreq->tail, length))
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
	fd = openat(dirfd, filename, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		afb_hreq_reply_error(hreq, MHD_HTTP_FORBIDDEN);
		return 1;
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
		if (verbose)
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
	if (verbose)
		fprintf(stderr, "redirect from [%s] to [%s]\n", hreq->url, url);
	return 1;
}

const char *afb_hreq_get_cookie(struct afb_hreq *hreq, const char *name)
{
	return MHD_lookup_connection_value(hreq->connection, MHD_COOKIE_KIND, name);
}

const char *afb_hreq_get_argument(struct afb_hreq *hreq, const char *name)
{
	return MHD_lookup_connection_value(hreq->connection, MHD_GET_ARGUMENT_KIND, name);
}

struct afb_req_itf afb_hreq_itf = {
	.get_cookie = (void*)afb_hreq_get_cookie,
	.get_argument = (void*)afb_hreq_get_argument
};

