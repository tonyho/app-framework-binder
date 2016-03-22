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


struct afb_req_post {
	const char *upload_data;
	size_t *upload_data_size;
};

struct afb_req {
	AFB_session *session;
	struct MHD_Connection *connection;
	enum afb_method method;
	const char *url;
	size_t lenurl;
	const char *tail;
	size_t lentail;
	struct afb_req **recorder;
	int (*post_handler) (struct afb_req *, struct afb_req_post *);
	int (*post_completed) (struct afb_req *, struct afb_req_post *);
	void *post_data;
};

struct afb_req_handler {
	struct afb_req_handler *next;
	const char *prefix;
	size_t length;
	int (*handler) (struct afb_req *, struct afb_req_post *, void *);
	void *data;
	int priority;
};

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
 * Removes the 'prefix' of 'length' frome the tail of 'request'
 * if and only if the prefix exists and is terminated by a leading
 * slash
 */
int afb_req_unprefix(struct afb_req *request, const char *prefix, size_t length)
{
	/* check the prefix ? */
	if (length > request->lentail || (request->tail[length] && request->tail[length] != '/')
	    || memcmp(prefix, request->tail, length))
		return 0;

	/* removes successives / */
	while (length < request->lentail && request->tail[length + 1] == '/')
		length++;

	/* update the tail */
	request->lentail -= length;
	request->tail += length;
	return 1;
}

int afb_req_valid_tail(struct afb_req *request)
{
	return validsubpath(request->tail);
}

void afb_req_reply_error(struct afb_req *request, unsigned int status)
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
	if (!MHD_queue_response(request->connection, status, response))
		fprintf(stderr, "Failed to reply error code %u", status);
	MHD_destroy_response(response);
}

int afb_request_redirect_to(struct afb_req *request, const char *url)
{
	struct MHD_Response *response;

	response = MHD_create_response_from_buffer(0, empty_string, MHD_RESPMEM_PERSISTENT);
	MHD_add_response_header(response, "Location", url);
	MHD_queue_response(request->connection, MHD_HTTP_MOVED_PERMANENTLY, response);
	MHD_destroy_response(response);
	if (verbose)
		fprintf(stderr, "redirect from [%s] to [%s]\n", request->url, url);
	return 1;
}

int afb_request_one_page_api_redirect(struct afb_req *request, struct afb_req_post *post, void *data)
{
	size_t plen;
	char *url;

	if (request->lentail >= 2 && request->tail[1] == '#')
		return 0;
	/*
	 * Here we have for example:
	 *    url  = "/pre/dir/page"   lenurl = 13
	 *    tail =     "/dir/page"   lentail = 9
	 *
	 * We will produce "/pre/#!dir/page"
	 *
	 * Let compute plen that include the / at end (for "/pre/")
	 */
	plen = request->lenurl - request->lentail + 1;
	url = alloca(request->lenurl + 3);
	memcpy(url, request->url, plen);
	url[plen++] = '#';
	url[plen++] = '!';
	memcpy(&url[plen], &request->tail[1], request->lentail);
	return afb_request_redirect_to(request, url);
}

struct afb_req_handler *afb_req_handler_new(struct afb_req_handler *head, const char *prefix,
					    int (*handler) (struct afb_req *, struct afb_req_post *, void *),
					    void *data, int priority)
{
	struct afb_req_handler *link, *iter, *previous;
	size_t length;

	/* get the length of the prefix without its leading / */
	length = strlen(prefix);
	while (length && prefix[length - 1] == '/')
		length--;

	/* allocates the new link */
	link = malloc(sizeof *link);
	if (link == NULL)
		return NULL;

	/* initialize it */
	link->prefix = prefix;
	link->length = length;
	link->handler = handler;
	link->data = data;
	link->priority = priority;

	/* adds it */
	previous = NULL;
	iter = head;
	while (iter && (priority < iter->priority || (priority == iter->priority && length <= iter->length))) {
		previous = iter;
		iter = iter->next;
	}
	link->next = iter;
	if (previous == NULL)
		return link;
	previous->next = link;
	return head;
}

int afb_req_add_handler(AFB_session * session, const char *prefix,
			int (*handler) (struct afb_req *, struct afb_req_post *, void *), void *data, int priority)
{
	struct afb_req_handler *head;

	head = afb_req_handler_new(session->handlers, prefix, handler, data, priority);
	if (head == NULL)
		return 0;
	session->handlers = head;
	return 1;
}

static int relay_to_doRestApi(struct afb_req *request, struct afb_req_post *post, void *data)
{
	return doRestApi(request->connection, request->session, &request->tail[1], get_method_name(request->method),
			 post->upload_data, post->upload_data_size, (void **)request->recorder);
}

static int afb_req_reply_file_if_exist(struct afb_req *request, int dirfd, const char *filename)
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
		afb_req_reply_error(request, MHD_HTTP_FORBIDDEN);
		return 1;
	}

	/* Retrieves file's status */
	if (fstat(fd, &st) != 0) {
		close(fd);
		afb_req_reply_error(request, MHD_HTTP_INTERNAL_SERVER_ERROR);
		return 1;
	}

	/* Don't serve directory */
	if (S_ISDIR(st.st_mode)) {
		rc = afb_req_reply_file_if_exist(request, fd, "index.html");
		close(fd);
		return rc;
	}

	/* Don't serve special files */
	if (!S_ISREG(st.st_mode)) {
		close(fd);
		afb_req_reply_error(request, MHD_HTTP_FORBIDDEN);
		return 1;
	}

	/* Check the method */
	if ((request->method & (afb_method_get | afb_method_head)) == 0) {
		close(fd);
		afb_req_reply_error(request, MHD_HTTP_METHOD_NOT_ALLOWED);
		return 1;
	}

	/* computes the etag */
	sprintf(etag, "%08X%08X", ((int)(st.st_mtim.tv_sec) ^ (int)(st.st_mtim.tv_nsec)), (int)(st.st_size));

	/* checks the etag */
	inm = MHD_lookup_connection_value(request->connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_NONE_MATCH);
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
			afb_req_reply_error(request, MHD_HTTP_INTERNAL_SERVER_ERROR);
			return 1;
		}

		/* create the response */
		response = MHD_create_response_from_fd((size_t) st.st_size, fd);
		status = MHD_HTTP_OK;

#if defined(USE_MAGIC_MIME_TYPE)
		/* set the type */
		if (request->session->magic) {
			const char *mimetype = magic_descriptor(request->session->magic, fd);
			if (mimetype != NULL)
				MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, mimetype);
		}
#endif
	}

	/* fills the value and send */
	MHD_add_response_header(response, MHD_HTTP_HEADER_CACHE_CONTROL, request->session->cacheTimeout);
	MHD_add_response_header(response, MHD_HTTP_HEADER_ETAG, etag);
	MHD_queue_response(request->connection, status, response);
	MHD_destroy_response(response);
	return 1;
}

static int afb_req_reply_file(struct afb_req *request, int dirfd, const char *filename)
{
	int rc = afb_req_reply_file_if_exist(request, dirfd, filename);
	if (rc == 0)
		afb_req_reply_error(request, MHD_HTTP_NOT_FOUND);
	return 1;
}

static int handle_alias(struct afb_req *request, struct afb_req_post *post, void *data)
{
	char *path;
	const char *alias = data;
	size_t lenalias;

	if (request->method != afb_method_get) {
		afb_req_reply_error(request, MHD_HTTP_METHOD_NOT_ALLOWED);
		return 1;
	}

	if (!validsubpath(request->tail)) {
		afb_req_reply_error(request, MHD_HTTP_FORBIDDEN);
		return 1;
	}

	lenalias = strlen(alias);
	path = alloca(lenalias + request->lentail + 1);
	memcpy(path, alias, lenalias);
	memcpy(&path[lenalias], request->tail, request->lentail + 1);
	return afb_req_reply_file(request, AT_FDCWD, path);
}

int afb_req_add_alias(AFB_session * session, const char *prefix, const char *alias, int priority)
{
	return afb_req_add_handler(session, prefix, handle_alias, (void *)alias, priority);
}

static int my_default_init(AFB_session * session)
{
	int idx;

	if (!afb_req_add_handler(session, session->config->rootapi, relay_to_doRestApi, NULL, 1))
		return 0;

	for (idx = 0; session->config->aliasdir[idx].url != NULL; idx++)
		if (!afb_req_add_alias
		    (session, session->config->aliasdir[idx].url, session->config->aliasdir[idx].path, 0))
			return 0;

	if (!afb_req_add_alias(session, "", session->config->rootdir, -10))
		return 0;

	if (!afb_req_add_handler(session, session->config->rootbase, afb_request_one_page_api_redirect, NULL, -20))
		return 0;

	return 1;
}

static int access_handler(void *cls,
			  struct MHD_Connection *connection,
			  const char *url,
			  const char *methodstr,
			  const char *version, const char *upload_data, size_t * upload_data_size, void **recorder)
{
	struct afb_req_post post;
	struct afb_req request;
	enum afb_method method;
	AFB_session *session;
	struct afb_req_handler *iter;

	session = cls;
	post.upload_data = upload_data;
	post.upload_data_size = upload_data_size;

#if 0
	struct afb_req *previous;

	previous = *recorder;
	if (previous) {
		assert((void **)previous->recorder == recorder);
		assert(previous->session == session);
		assert(previous->connection == connection);
		assert(previous->method == get_method(methodstr));
		assert(previous->url == url);

		/* TODO */
/*
		assert(previous->post_handler != NULL);
		previous->post_handler(previous, &post);
		return MHD_NO;
*/
	}
#endif

	method = get_method(methodstr);
	if (method == afb_method_none) {
		afb_req_reply_error(&request, MHD_HTTP_BAD_REQUEST);
		return MHD_YES;
	}

	/* init the request */
	request.session = cls;
	request.connection = connection;
	request.method = method;
	request.tail = request.url = url;
	request.lentail = request.lenurl = strlen(url);
	request.recorder = (struct afb_req **)recorder;
	request.post_handler = NULL;
	request.post_completed = NULL;
	request.post_data = NULL;

	/* search an handler for the request */
	iter = session->handlers;
	while (iter) {
		if (afb_req_unprefix(&request, iter->prefix, iter->length)) {
			if (iter->handler(&request, &post, iter->data))
				return MHD_YES;
			request.tail = request.url;
			request.lentail = request.lenurl;
		}
		iter = iter->next;
	}

	/* no handler */
	afb_req_reply_error(&request, method != afb_method_get ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_NOT_FOUND);
	return MHD_YES;
}

/* Because of POST call multiple time requestApi we need to free POST handle here */
static void end_handler(void *cls, struct MHD_Connection *connection, void **con_cls,
			enum MHD_RequestTerminationCode toe)
{
	AFB_PostHandle *posthandle = *con_cls;

	/* if post handle was used let's free everything */
	if (posthandle != NULL)
		endPostRequest(posthandle);
}

static int new_client_handler(void *cls, const struct sockaddr *addr, socklen_t addrlen)
{
	return MHD_YES;
}

#if defined(USE_MAGIC_MIME_TYPE)

#if !defined(MAGIC_DB)
#define MAGIC_DB "/usr/share/misc/magic.mgc"
#endif

static int init_lib_magic (AFB_session *session)
{
	/* MAGIC_MIME tells magic to return a mime of the file, but you can specify different things */
	if (verbose)
		printf("Loading mimetype default magic database\n");

	session->magic = magic_open(MAGIC_MIME_TYPE);
	if (session->magic == NULL) {
		fprintf(stderr,"ERROR: unable to initialize magic library\n");
		return 0;
	}

	/* Warning: should not use NULL for DB [libmagic bug wont pass efence check] */
	if (magic_load(session->magic, MAGIC_DB) != 0) {
		fprintf(stderr,"cannot load magic database - %s\n", magic_error(session->magic));
/*
		magic_close(session->magic);
		session->magic = NULL;
		return 0;
*/
	}

	return 1;
}
#endif

AFB_error httpdStart(AFB_session * session)
{

	if (!my_default_init(session)) {
		printf("Error: initialisation of httpd failed");
		return AFB_FATAL;
	}

	/* Initialise Client Session Hash Table */
	ctxStoreInit(CTX_NBCLIENTS);

#if defined(USE_MAGIC_MIME_TYPE)
	/*TBD open libmagic cache [fail to pass EFENCE check (allocating 0 bytes)] */
	init_lib_magic (session);
#endif

	if (verbose) {
		printf("AFB:notice Waiting port=%d rootdir=%s\n", session->config->httpdPort, session->config->rootdir);
		printf("AFB:notice Browser URL= http:/*localhost:%d\n", session->config->httpdPort);
	}

	session->httpd = MHD_start_daemon(
		MHD_USE_EPOLL_LINUX_ONLY | MHD_USE_TCP_FASTOPEN | MHD_USE_DEBUG,
		(uint16_t) session->config->httpdPort,	/* port */
		new_client_handler, NULL,	/* Tcp Accept call back + extra attribute */
		access_handler, session,	/* Http Request Call back + extra attribute */
		MHD_OPTION_NOTIFY_COMPLETED, end_handler, session,
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)15,	/* 15 seconds */
		MHD_OPTION_END);	/* options-end */

	if (session->httpd == NULL) {
		printf("Error: httpStart invalid httpd port: %d", session->config->httpdPort);
		return AFB_FATAL;
	}
	return AFB_SUCCESS;
}

/* infinite loop */
AFB_error httpdLoop(AFB_session * session)
{
	int count = 0;
	const union MHD_DaemonInfo *info;
	struct pollfd pfd;

	info = MHD_get_daemon_info(session->httpd, MHD_DAEMON_INFO_EPOLL_FD_LINUX_ONLY);
	if (info == NULL) {
		printf("Error: httpLoop no pollfd");
		goto error;
	}
	pfd.fd = info->listen_fd;
	pfd.events = POLLIN;

	if (verbose)
		fprintf(stderr, "AFB:notice entering httpd waiting loop\n");
	while (TRUE) {
		if (verbose)
			fprintf(stderr, "AFB:notice httpd alive [%d]\n", count++);
		poll(&pfd, 1, 15000);	/* 15 seconds (as above timeout when starting) */
		MHD_run(session->httpd);
	}

 error:
	/* should never return from here */
	return AFB_FATAL;
}

int httpdStatus(AFB_session * session)
{
	return MHD_run(session->httpd);
}

void httpdStop(AFB_session * session)
{
	MHD_stop_daemon(session->httpd);
}
