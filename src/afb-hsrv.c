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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <microhttpd.h>

#include "local-def.h"
#include "afb-method.h"
#include "afb-hreq.h"
#include "afb-hsrv.h"
#include "afb-websock.h"
#include "afb-apis.h"
#include "afb-req-itf.h"
#include "verbose.h"
#include "utils-upoll.h"

#define JSON_CONTENT  "application/json"
#define FORM_CONTENT  MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA


struct afb_hsrv_handler {
	struct afb_hsrv_handler *next;
	const char *prefix;
	size_t length;
	int (*handler) (struct afb_hreq *, void *);
	void *data;
	int priority;
};

struct afb_diralias {
	const char *alias;
	const char *directory;
	size_t lendir;
	int dirfd;
};

struct afb_hsrv {
	struct MHD_Daemon *httpd;
	struct afb_hsrv_handler *handlers;
	struct upoll *upoll;
};

static struct upoll *upoll = NULL;

static struct afb_hsrv_handler *new_handler(
		struct afb_hsrv_handler *head,
		const char *prefix,
		int (*handler) (struct afb_hreq *, void *),
		void *data,
		int priority)
{
	struct afb_hsrv_handler *link, *iter, *previous;
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

int afb_hsrv_add_handler(
		AFB_session * session,
		const char *prefix,
		int (*handler) (struct afb_hreq *, void *),
		void *data,
		int priority)
{
	struct afb_hsrv_handler *head;

	head = new_handler(session->handlers, prefix, handler, data, priority);
	if (head == NULL)
		return 0;
	session->handlers = head;
	return 1;
}

int afb_hreq_one_page_api_redirect(
		struct afb_hreq *hreq,
		void *data)
{
	size_t plen;
	char *url;

	if (hreq->lentail >= 2 && hreq->tail[1] == '#')
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
	plen = hreq->lenurl - hreq->lentail + 1;
	url = alloca(hreq->lenurl + 3);
	memcpy(url, hreq->url, plen);
	url[plen++] = '#';
	url[plen++] = '!';
	memcpy(&url[plen], &hreq->tail[1], hreq->lentail);
	return afb_hreq_redirect_to(hreq, url);
}

static int afb_hreq_websocket_switch(struct afb_hreq *hreq, void *data)
{
	int later;

	afb_hreq_context(hreq);
	if (hreq->lentail != 0 || !afb_websock_check(hreq, &later))
		return 0;

	if (!later) {
		struct afb_websock *ws = afb_websock_create(hreq);
		if (ws != NULL)
			hreq->upgrade = 1;
	}
	return 1;
}

static int afb_hreq_rest_api(struct afb_hreq *hreq, void *data)
{
	const char *api, *verb;
	size_t lenapi, lenverb;
	struct AFB_clientCtx *context;

	api = &hreq->tail[strspn(hreq->tail, "/")];
	lenapi = strcspn(api, "/");
	verb = &api[lenapi];
	verb = &verb[strspn(verb, "/")];
	lenverb = strcspn(verb, "/");

	if (!(*api && *verb && lenapi && lenverb))
		return 0;

	context = afb_hreq_context(hreq);
	return afb_apis_handle(afb_hreq_to_req(hreq), context, api, lenapi, verb, lenverb);
}

static int handle_alias(struct afb_hreq *hreq, void *data)
{
	struct afb_diralias *da = data;

	if (hreq->method != afb_method_get) {
		afb_hreq_reply_error(hreq, MHD_HTTP_METHOD_NOT_ALLOWED);
		return 1;
	}

	if (!afb_hreq_valid_tail(hreq)) {
		afb_hreq_reply_error(hreq, MHD_HTTP_FORBIDDEN);
		return 1;
	}

	return afb_hreq_reply_file(hreq, da->dirfd, &hreq->tail[1]);
}

int afb_hsrv_add_alias(AFB_session * session, const char *prefix, const char *alias, int priority)
{
	struct afb_diralias *da;
	int dirfd;

	dirfd = open(alias, O_PATH|O_DIRECTORY);
	if (dirfd < 0) {
		/* TODO message */
		return 0;
	}
	da = malloc(sizeof *da);
	if (da != NULL) {
		da->alias = prefix;
		da->directory = alias;
		da->lendir = strlen(da->directory);
		da->dirfd = dirfd;
		if (afb_hsrv_add_handler(session, prefix, handle_alias, da, priority))
			return 1;
		free(da);
	}
	close(dirfd);
	return 0;
}

void afb_hsrv_reply_error(struct MHD_Connection *connection, unsigned int status)
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
	if (!MHD_queue_response(connection, status, response))
		fprintf(stderr, "Failed to reply error code %u", status);
	MHD_destroy_response(response);
}

static int postproc(void *cls,
                    enum MHD_ValueKind kind,
                    const char *key,
                    const char *filename,
                    const char *content_type,
                    const char *transfer_encoding,
                    const char *data,
		    uint64_t off,
		    size_t size)
{
	struct afb_hreq *hreq = cls;
	if (filename != NULL)
		return afb_hreq_post_add_file(hreq, key, filename, data, size);
	else
		return afb_hreq_post_add(hreq, key, data, size);
}

static int access_handler(
		void *cls,
		struct MHD_Connection *connection,
		const char *url,
		const char *methodstr,
		const char *version,
		const char *upload_data,
		size_t *upload_data_size,
		void **recordreq)
{
	int rc;
	struct afb_hreq *hreq;
	enum afb_method method;
	AFB_session *session;
	struct afb_hsrv_handler *iter;
	const char *type;

	session = cls;
	hreq = *recordreq;
	if (hreq == NULL) {
		/* create the request */
		hreq = calloc(1, sizeof *hreq);
		if (hreq == NULL)
			goto internal_error;
		*recordreq = hreq;

		/* get the method */
		method = get_method(methodstr);
		method &= afb_method_get | afb_method_post;
		if (method == afb_method_none)
			goto bad_request;

		/* init the request */
		hreq->session = cls;
		hreq->connection = connection;
		hreq->method = method;
		hreq->version = version;
		hreq->tail = hreq->url = url;
		hreq->lentail = hreq->lenurl = strlen(url);

		/* init the post processing */
		if (method == afb_method_post) {
			type = afb_hreq_get_header(hreq, MHD_HTTP_HEADER_CONTENT_TYPE);
			if (type == NULL) {
				/* an empty post, let's process it as a get */
				hreq->method = afb_method_get;
			} else if (strcasestr(type, FORM_CONTENT) != NULL) {
				hreq->postform = MHD_create_post_processor (connection, 65500, postproc, hreq);
				if (hreq->postform == NULL)
					goto internal_error;
				return MHD_YES;
			} else if (strcasestr(type, JSON_CONTENT) == NULL) {
				afb_hsrv_reply_error(connection, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE);
				return MHD_YES;
			}
		}
	}

	/* process further data */
	if (*upload_data_size) {
		if (hreq->postform != NULL) {
			if (!MHD_post_process (hreq->postform, upload_data, *upload_data_size))
				goto internal_error;
		} else {
			if (!afb_hreq_post_add(hreq, NULL, upload_data, *upload_data_size))
				goto internal_error;
		}
		*upload_data_size = 0;
		return MHD_YES;		
	}

	/* flush the data */
	if (hreq->postform != NULL) {
		rc = MHD_destroy_post_processor(hreq->postform);
		hreq->postform = NULL;
		if (rc == MHD_NO)
			goto bad_request;
	}

	/* search an handler for the request */
	iter = session->handlers;
	while (iter) {
		if (afb_hreq_unprefix(hreq, iter->prefix, iter->length)) {
			if (iter->handler(hreq, iter->data))
				return MHD_YES;
			hreq->tail = hreq->url;
			hreq->lentail = hreq->lenurl;
		}
		iter = iter->next;
	}

	/* no handler */
	afb_hreq_reply_error(hreq, MHD_HTTP_NOT_FOUND);
	return MHD_YES;

bad_request:
	afb_hsrv_reply_error(connection, MHD_HTTP_BAD_REQUEST);
	return MHD_YES;

internal_error:
	afb_hsrv_reply_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	return MHD_YES;
}

/* Because of POST call multiple time requestApi we need to free POST handle here */
static void end_handler(void *cls, struct MHD_Connection *connection, void **recordreq,
			enum MHD_RequestTerminationCode toe)
{
	struct afb_hreq *hreq;

	hreq = *recordreq;
	if (hreq->upgrade)
		MHD_suspend_connection (connection);
	afb_hreq_free(hreq);
}

static int new_client_handler(void *cls, const struct sockaddr *addr, socklen_t addrlen)
{
	return MHD_YES;
}

static int my_default_init(AFB_session * session)
{
	int idx;

	if (!afb_hsrv_add_handler(session, session->config->rootapi, afb_hreq_websocket_switch, NULL, 20))
		return 0;

	if (!afb_hsrv_add_handler(session, session->config->rootapi, afb_hreq_rest_api, NULL, 10))
		return 0;

	for (idx = 0; session->config->aliasdir[idx].url != NULL; idx++)
		if (!afb_hsrv_add_alias (session, session->config->aliasdir[idx].url, session->config->aliasdir[idx].path, 0))
			return 0;

	if (!afb_hsrv_add_alias(session, "", session->config->rootdir, -10))
		return 0;

	if (!afb_hsrv_add_handler(session, session->config->rootbase, afb_hreq_one_page_api_redirect, NULL, -20))
		return 0;

	return 1;
}

/* infinite loop */
static void hsrv_handle_event(struct MHD_Daemon *httpd)
{
	MHD_run(httpd);
}

int afb_hsrv_start(AFB_session * session)
{
	struct MHD_Daemon *httpd;
	const union MHD_DaemonInfo *info;

	if (!my_default_init(session)) {
		printf("Error: initialisation of httpd failed");
		return 0;
	}

	if (verbosity) {
		printf("AFB:notice Waiting port=%d rootdir=%s\n", session->config->httpdPort, session->config->rootdir);
		printf("AFB:notice Browser URL= http:/*localhost:%d\n", session->config->httpdPort);
	}

	httpd = MHD_start_daemon(
		MHD_USE_EPOLL_LINUX_ONLY | MHD_USE_TCP_FASTOPEN | MHD_USE_DEBUG | MHD_USE_SUSPEND_RESUME,
		(uint16_t) session->config->httpdPort,	/* port */
		new_client_handler, NULL,	/* Tcp Accept call back + extra attribute */
		access_handler, session,	/* Http Request Call back + extra attribute */
		MHD_OPTION_NOTIFY_COMPLETED, end_handler, session,
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)15,	/* 15 seconds */
		MHD_OPTION_END);	/* options-end */

	if (httpd == NULL) {
		printf("Error: httpStart invalid httpd port: %d", session->config->httpdPort);
		return 0;
	}

	info = MHD_get_daemon_info(httpd, MHD_DAEMON_INFO_EPOLL_FD_LINUX_ONLY);
	if (info == NULL) {
		MHD_stop_daemon(httpd);
		fprintf(stderr, "Error: httpStart no pollfd");
		return 0;
	}

	upoll = upoll_open(info->listen_fd, httpd);
	if (upoll == NULL) {
		MHD_stop_daemon(httpd);
		fprintf(stderr, "Error: connection to upoll of httpd failed");
		return 0;
	}
	upoll_on_readable(upoll, (void*)hsrv_handle_event);

	session->httpd = httpd;
	return 1;
}

void afb_hsrv_stop(AFB_session * session)
{
	if (upoll)
		upoll_close(upoll);
	upoll = NULL;
	if (session->httpd != NULL)
		MHD_stop_daemon(session->httpd);
	session->httpd = NULL;
}

