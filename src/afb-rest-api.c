/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
 * Author José Bollo <jose.bollo@iot.bzh>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Contain all generic part to handle REST/API
 * 
 *  https://www.gnu.org/software/libmicrohttpd/tutorial.html [search 'largepost.c']
 */

#define _GNU_SOURCE

#include "../include/local-def.h"

#include <dirent.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>

#include "afb-apis.h"
#include "session.h"

#define AFB_MSG_JTYPE "AJB_reply"

#define JSON_CONTENT  "application/json"
#define FORM_CONTENT  MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA

static json_object *afbJsonType;

// Because of POST call multiple time requestApi we need to free POST handle here
// Note this method is called from http-svc just before closing session
PUBLIC void endPostRequest(AFB_PostHandle * postHandle)
{

	if (postHandle->type == AFB_POST_JSON) {
		// if (verbose) fprintf(stderr, "End PostJson Request UID=%d\n", postHandle->uid);
	}

	if (postHandle->type == AFB_POST_FORM) {
		if (verbose)
			fprintf(stderr, "End PostForm Request UID=%d\n", postHandle->uid);
	}
	if (postHandle->privatebuf)
		free(postHandle->privatebuf);
	free(postHandle);
}

// Check of apiurl is declare in this plugin and call it
static AFB_error doCallPluginApi(AFB_request * request, int apiidx, int verbidx, void *context)
{
	enum AFB_sessionE session;
	json_object *jresp, *jcall, *jreqt;
	AFB_clientCtx *clientCtx = NULL;

	// Request was found and at least partially executed
	jreqt = json_object_new_object();
	json_object_object_add(jreqt, "jtype", json_object_get(afbJsonType));

	// prepare an object to store calling values
	jcall = json_object_new_object();
	json_object_object_add(jcall, "prefix", json_object_new_string(request->prefix));
	json_object_object_add(jcall, "api", json_object_new_string(request->method));

	// Out of SessionNone every call get a client context session
	session = afb_apis_get(apiidx, verbidx)->session;
	if (AFB_SESSION_NONE != session) {

		// add client context to request
		clientCtx = ctxClientGet(request);
		if (clientCtx == NULL) {
			request->errcode = MHD_HTTP_INSUFFICIENT_STORAGE;
			json_object_object_add(jcall, "status", json_object_new_string("fail"));
			json_object_object_add(jcall, "info", json_object_new_string("Client Session Context Full !!!"));
			json_object_object_add(jreqt, "request", jcall);
			goto ExitOnDone;
		}
		request->context = clientCtx->contexts[apiidx];
		request->uuid = clientCtx->uuid;

		if (verbose)
			fprintf(stderr, "Plugin=[%s] Api=[%s] Middleware=[%d] Client=[%p] Uuid=[%s] Token=[%s]\n", request->prefix, request->method, session, clientCtx, clientCtx->uuid, clientCtx->token);

		switch (session) {

		case AFB_SESSION_CREATE:
			if (clientCtx->token[0] != '\0' && request->config->token[0] != '\0') {
				request->errcode = MHD_HTTP_UNAUTHORIZED;
				json_object_object_add(jcall, "status", json_object_new_string("exist"));
				json_object_object_add(jcall, "info", json_object_new_string("AFB_SESSION_CREATE Session already exist"));
				json_object_object_add(jreqt, "request", jcall);
				goto ExitOnDone;
			}

			if (AFB_SUCCESS != ctxTokenCreate(clientCtx, request)) {
				request->errcode = MHD_HTTP_UNAUTHORIZED;
				json_object_object_add(jcall, "status", json_object_new_string("fail"));
				json_object_object_add(jcall, "info", json_object_new_string("AFB_SESSION_CREATE Invalid Initial Token"));
				json_object_object_add(jreqt, "request", jcall);
				goto ExitOnDone;
			} else {
				json_object_object_add(jcall, "uuid", json_object_new_string(clientCtx->uuid));
				json_object_object_add(jcall, "token", json_object_new_string(clientCtx->token));
				json_object_object_add(jcall, "timeout", json_object_new_int(request->config->cntxTimeout));
			}
			break;

		case AFB_SESSION_RENEW:
			if (AFB_SUCCESS != ctxTokenRefresh(clientCtx, request)) {
				request->errcode = MHD_HTTP_UNAUTHORIZED;
				json_object_object_add(jcall, "status", json_object_new_string("fail"));
				json_object_object_add(jcall, "info", json_object_new_string("AFB_SESSION_REFRESH Broken Exchange Token Chain"));
				json_object_object_add(jreqt, "request", jcall);
				goto ExitOnDone;
			} else {
				json_object_object_add(jcall, "uuid", json_object_new_string(clientCtx->uuid));
				json_object_object_add(jcall, "token", json_object_new_string(clientCtx->token));
				json_object_object_add(jcall, "timeout", json_object_new_int(request->config->cntxTimeout));
			}
			break;

		case AFB_SESSION_CLOSE:
			if (AFB_SUCCESS != ctxTokenCheck(clientCtx, request)) {
				request->errcode = MHD_HTTP_UNAUTHORIZED;
				json_object_object_add(jcall, "status", json_object_new_string("empty"));
				json_object_object_add(jcall, "info", json_object_new_string("AFB_SESSION_CLOSE Not a Valid Access Token"));
				json_object_object_add(jreqt, "request", jcall);
				goto ExitOnDone;
			} else {
				json_object_object_add(jcall, "uuid", json_object_new_string(clientCtx->uuid));
			}
			break;

		case AFB_SESSION_CHECK:
		default:
			// default action is check
			if (AFB_SUCCESS != ctxTokenCheck(clientCtx, request)) {
				request->errcode = MHD_HTTP_UNAUTHORIZED;
				json_object_object_add(jcall, "status", json_object_new_string("fail"));
				json_object_object_add(jcall, "info", json_object_new_string("AFB_SESSION_CHECK Invalid Active Token"));
				json_object_object_add(jreqt, "request", jcall);
				goto ExitOnDone;
			}
			break;
		}
	}

	// Effectively CALL PLUGIN API with a subset of the context
	jresp = afb_apis_get(apiidx, verbidx)->callback(request, context);

	// Store context in case it was updated by plugins
	if (request->context != NULL)
		clientCtx->contexts[apiidx] = request->context;

	// handle intermediary Post Iterates out of band
	if ((jresp == NULL) && (request->errcode == MHD_HTTP_OK))
		return AFB_SUCCESS;

	// Session close is done after the API call so API can still use session in closing API
	if (AFB_SESSION_CLOSE == session)
		ctxTokenReset(clientCtx, request);

	// API should return NULL of a valid Json Object
	if (jresp == NULL) {
		json_object_object_add(jcall, "status", json_object_new_string("null"));
		json_object_object_add(jreqt, "request", jcall);
		request->errcode = MHD_HTTP_NO_RESPONSE;

	} else {
		json_object_object_add(jcall, "status", json_object_new_string("processed"));
		json_object_object_add(jreqt, "request", jcall);
		json_object_object_add(jreqt, "response", jresp);
	}

ExitOnDone:
	request->jresp = jreqt;
	return AFB_DONE;
}

// Check of apiurl is declare in this plugin and call it
extern __thread sigjmp_buf *error_handler;
static AFB_error callPluginApi(AFB_request * request, int apiidx, int verbidx, void *context)
{
	sigjmp_buf jmpbuf, *older;

	json_object *jcall, *jreqt;
	int status;

	// save context before calling the API
	status = setjmp(jmpbuf);
	if (status != 0) {

		// Request was found and at least partially executed
		jreqt = json_object_new_object();
		json_object_object_add(jreqt, "jtype", json_object_get(afbJsonType));

		// prepare an object to store calling values
		jcall = json_object_new_object();
		json_object_object_add(jcall, "prefix", json_object_new_string(request->prefix));
		json_object_object_add(jcall, "api", json_object_new_string(request->method));

		// Plugin aborted somewhere during its execution
		json_object_object_add(jcall, "status", json_object_new_string("abort"));
		json_object_object_add(jcall, "info", json_object_new_string("Plugin broke during execution"));
		json_object_object_add(jreqt, "request", jcall);
		request->jresp = jreqt;
	} else {

		// Trigger a timer to protect from unacceptable long time execution
		if (request->config->apiTimeout > 0)
			alarm((unsigned)request->config->apiTimeout);

		older = error_handler;
		error_handler = &jmpbuf;
		doCallPluginApi(request, apiidx, verbidx, context);
		error_handler = older;

		// cancel timeout and plugin signal handle before next call
		alarm(0);
	}
	return AFB_DONE;
}

STATIC AFB_error findAndCallApi(AFB_request * request, void *context)
{
	int apiidx, verbidx;
	AFB_error status;

	if (!request->method || !request->prefix)
		return AFB_FAIL;

	/* get the plugin if any */
	apiidx = afb_apis_get_apiidx(request->prefix, 0);
	if (apiidx < 0) {
		request->jresp = jsonNewMessage(AFB_FATAL, "No Plugin=[%s] Url=%s", request->prefix, request->url);
		request->errcode = MHD_HTTP_UNPROCESSABLE_ENTITY;
		return AFB_FAIL;
	}

	/* get the verb if any */
	verbidx = afb_apis_get_verbidx(apiidx, request->method);
	if (verbidx < 0) {
		request->jresp = jsonNewMessage(AFB_FATAL, "No API=[%s] for Plugin=[%s] url=[%s]", request->method, request->prefix, request->url);
		request->errcode = MHD_HTTP_UNPROCESSABLE_ENTITY;
		return AFB_FAIL;
	}

	/* Search for a plugin with this urlpath */
	status = callPluginApi(request, apiidx, verbidx, context);

	/* plugin callback did not return a valid Json Object */
	if (status == AFB_FAIL) {
		request->jresp = jsonNewMessage(AFB_FATAL, "No API=[%s] for Plugin=[%s] url=[%s]", request->method, request->prefix, request->url);
		request->errcode = MHD_HTTP_UNPROCESSABLE_ENTITY;
		return AFB_FAIL;
	}
	// Everything look OK
	return status;
}

// This CB is call for every item with a form post it reformat iterator values
// and callback Plugin API for each Item within PostForm.
STATIC int doPostIterate(void *cls, enum MHD_ValueKind kind, const char *key, const char *filename, const char *mimetype, const char *encoding, const char *data, uint64_t offset, size_t size)
{

	AFB_error status;
	AFB_PostItem item;

	// retrieve API request from Post iterator handle  
	AFB_PostHandle *postHandle = (AFB_PostHandle *) cls;
	AFB_request *request = (AFB_request *) postHandle->privatebuf;
	AFB_PostRequest postRequest;

	if (verbose)
		fprintf(stderr, "postHandle key=%s filename=%s len=%zu mime=%s\n", key, filename, size, mimetype);

	// Create and Item value for Plugin API
	item.kind = kind;
	item.key = key;
	item.filename = filename;
	item.mimetype = mimetype;
	item.encoding = encoding;
	item.len = size;
	item.data = data;
	item.offset = offset;

	// Reformat Request to make it somehow similar to GET/PostJson case
	postRequest.data = (char *)postHandle;
	postRequest.len = size;
	postRequest.type = AFB_POST_FORM;;
	request->post = &postRequest;

	// effectively call plugin API                 
	status = findAndCallApi(request, &item);
	// when returning no processing of postform stop
	if (status != AFB_SUCCESS)
		return MHD_NO;

	// let's allow iterator to move to next item
	return MHD_YES;
}

STATIC void freeRequest(AFB_request * request)
{

	free(request->prefix);
	free(request->method);
	free(request);
}

STATIC AFB_request *createRequest(struct MHD_Connection *connection, AFB_session * session, const char *url)
{

	AFB_request *request;

	// Start with a clean request
	request = calloc(1, sizeof(AFB_request));
	char *urlcpy1, *urlcpy2;
	char *baseapi, *baseurl;

	// Extract plugin urlpath from request and make two copy because strsep overload copy
	urlcpy1 = urlcpy2 = strdup(url);
	baseurl = strsep(&urlcpy2, "/");
	if (baseurl == NULL) {
		request->jresp = jsonNewMessage(AFB_FATAL, "Invalid API call url=[%s]", url);
		request->errcode = MHD_HTTP_BAD_REQUEST;
		goto Done;
	}
	// let's compute URL and call API
	baseapi = strsep(&urlcpy2, "/");
	if (baseapi == NULL) {
		request->jresp = jsonNewMessage(AFB_FATAL, "Invalid API call plugin=[%s] url=[%s]", baseurl, url);
		request->errcode = MHD_HTTP_BAD_REQUEST;
		goto Done;
	}
	// build request structure
	request->connection = connection;
	request->config = session->config;
	request->url = url;
	request->prefix = strdup(baseurl);
	request->method = strdup(baseapi);

 Done:
	free(urlcpy1);
	return (request);
}















static int doRestApiPost(struct MHD_Connection *connection, AFB_session * session, const char *url, const char *method, const char *upload_data, size_t * upload_data_size, void **con_cls)
{

	static int postcount = 0;	// static counter to debug POST protocol
	json_object *errMessage;
	AFB_error status;
	struct MHD_Response *webResponse;
	const char *serialized;
	AFB_request *request = NULL;
	AFB_PostHandle *postHandle;
	AFB_PostRequest postRequest;
	int ret;

	// fprintf (stderr, "doRestAPI method=%s posthandle=%p\n", method, con_cls);

	// if post data may come in multiple calls
	const char *encoding, *param;
	int contentlen = -1;
	postHandle = *con_cls;

	// This is the initial post event let's create form post structure POST data come in multiple events
	if (postHandle == NULL) {

		// allocate application POST processor handle to zero
		postHandle = calloc(1, sizeof(AFB_PostHandle));
		postHandle->uid = postcount++;	// build a UID for DEBUG
		*con_cls = postHandle;	// update context with posthandle

		// Let make sure we have the right encoding and a valid length
		encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_TYPE);

		// We are facing an empty post let's process it as a get
		if (encoding == NULL) {
			postHandle->type = AFB_POST_EMPTY;
			return MHD_YES;
		}

		// Form post is handle through a PostProcessor and call API once per form key
		if (strcasestr(encoding, FORM_CONTENT) != NULL) {
			if (verbose)
				fprintf(stderr, "Create doPostIterate[uid=%d posthandle=%p]\n", postHandle->uid, postHandle);

			request = createRequest(connection, session, url);
			if (request->jresp != NULL)
				goto ProcessApiCall;
			postHandle->type = AFB_POST_FORM;
			postHandle->privatebuf = (void *)request;
			postHandle->pp = MHD_create_post_processor(connection, MAX_POST_SIZE, &doPostIterate, postHandle);

			if (NULL == postHandle->pp) {
				fprintf(stderr, "OOPS: Internal error fail to allocate MHD_create_post_processor\n");
				free(postHandle);
				return MHD_NO;
			}
			return MHD_YES;
		}
		// POST json is store into a buffer and present in one piece to API
		if (strcasestr(encoding, JSON_CONTENT) != NULL) {

			param = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_LENGTH);
			if (param)
				sscanf(param, "%i", &contentlen);

			// Because PostJson are build in RAM size is constrained
			if (contentlen > MAX_POST_SIZE) {
				errMessage = jsonNewMessage(AFB_FATAL, "Post Date to big %d > %d", contentlen, MAX_POST_SIZE);
				goto ExitOnError;
			}
			// Size is OK, let's allocate a buffer to hold post data
			postHandle->type = AFB_POST_JSON;
			postHandle->privatebuf = malloc((unsigned)contentlen + 1);	// allocate memory for full POST data + 1 for '\0' enf of string

			// if (verbose) fprintf(stderr, "Create PostJson[uid=%d] Size=%d\n", postHandle->uid, contentlen);
			return MHD_YES;

		}
		// We only support Json and Form Post format
		errMessage = jsonNewMessage(AFB_FATAL, "Post Date wrong type encoding=%s != %s", encoding, JSON_CONTENT);
		goto ExitOnError;
	}

	// This time we receive partial/all Post data. Note that even if we get all POST data. We should nevertheless
	// return MHD_YES and not process the request directly. Otherwise Libmicrohttpd is unhappy and fails with
	// 'Internal application error, closing connection'.            
	if (*upload_data_size) {

		if (postHandle->type == AFB_POST_FORM) {
			// if (verbose) fprintf(stderr, "Processing PostForm[uid=%d]\n", postHandle->uid);
			MHD_post_process(postHandle->pp, upload_data, *upload_data_size);
		}
		// Process JsonPost request when buffer is completed let's call API    
		if (postHandle->type == AFB_POST_JSON) {
			// if (verbose) fprintf(stderr, "Updating PostJson[uid=%d]\n", postHandle->uid);
			memcpy(&postHandle->privatebuf[postHandle->len], upload_data, *upload_data_size);
			postHandle->len = postHandle->len + *upload_data_size;
		}

		*upload_data_size = 0;
		return MHD_YES;

	}

	if (postHandle->type == AFB_POST_FORM)
		request = postHandle->privatebuf;
	else
		// Create a request structure to finalise the request
		request = createRequest(connection, session, url);

	if (request->jresp != NULL) {
		errMessage = request->jresp;
		goto ExitOnError;
	}
	postRequest.type = postHandle->type;

	// Postform add application context handle to request
	if (postHandle->type == AFB_POST_FORM) {
		postRequest.data = (char *)postHandle;
		request->post = &postRequest;
	}

	if (postHandle->type == AFB_POST_JSON) {
		// if (verbose) fprintf(stderr, "Processing PostJson[uid=%d]\n", postHandle->uid);

		param = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_LENGTH);
		if (param)
			sscanf(param, "%i", &contentlen);

		// At this level we're may verify that we got everything and process DATA
		if (postHandle->len != contentlen) {
			errMessage = jsonNewMessage(AFB_FATAL, "Post Data Incomplete UID=%d Len %d != %d", postHandle->uid, contentlen, postHandle->len);
			goto ExitOnError;
		}
		// Before processing data, make sure buffer string is properly ended
		postHandle->privatebuf[postHandle->len] = '\0';
		postRequest.data = postHandle->privatebuf;
		request->post = &postRequest;

		// if (verbose) fprintf(stderr, "Close Post[%d] Buffer=%s\n", postHandle->uid, request->post->data);
	}

 ProcessApiCall:
	// Request is ready let's call API without any extra handle
	status = findAndCallApi(request, NULL);

	serialized = json_object_to_json_string(request->jresp);
	webResponse = MHD_create_response_from_buffer(strlen(serialized), (void *)serialized, MHD_RESPMEM_MUST_COPY);

	// client did not pass token on URI let's use cookies 
	if ((!request->restfull) && (request->context != NULL)) {
		char cookie[256];
		snprintf(cookie, sizeof(cookie), "%s-%d=%s; Path=%s; Max-Age=%d; HttpOnly", COOKIE_NAME, request->config->httpdPort, request->uuid, request->config->rootapi, request->config->cntxTimeout);
		MHD_add_response_header(webResponse, MHD_HTTP_HEADER_SET_COOKIE, cookie);
	}
	// if requested add an error status
	if (request->errcode != 0)
		ret = MHD_queue_response(connection, request->errcode, webResponse);
	else
		MHD_queue_response(connection, MHD_HTTP_OK, webResponse);

	MHD_destroy_response(webResponse);
	json_object_put(request->jresp);	// decrease reference rqtcount to free the json object
	freeRequest(request);
	return MHD_YES;

 ExitOnError:
	freeRequest(request);
	serialized = json_object_to_json_string(errMessage);
	webResponse = MHD_create_response_from_buffer(strlen(serialized), (void *)serialized, MHD_RESPMEM_MUST_COPY);
	MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, webResponse);
	MHD_destroy_response(webResponse);
	json_object_put(errMessage);	// decrease reference rqtcount to free the json object
	return MHD_YES;
}




















static int doRestApiGet(struct MHD_Connection *connection, AFB_session * session, const char *url, const char *method, const char *upload_data, size_t * upload_data_size, void **con_cls)
{
	AFB_error status;
	struct MHD_Response *webResponse;
	const char *serialized;
	AFB_request *request = NULL;
	int ret;

	// fprintf (stderr, "doRestAPI method=%s posthandle=%p\n", method, con_cls);

	// if post data may come in multiple calls
	// this is a get we only need a request
	request = createRequest(connection, session, url);

	// Request is ready let's call API without any extra handle
	status = findAndCallApi(request, NULL);

	serialized = json_object_to_json_string(request->jresp);
	webResponse = MHD_create_response_from_buffer(strlen(serialized), (void *)serialized, MHD_RESPMEM_MUST_COPY);

	// client did not pass token on URI let's use cookies 
	if ((!request->restfull) && (request->context != NULL)) {
		char cookie[256];
		snprintf(cookie, sizeof(cookie), "%s-%d=%s; Path=%s; Max-Age=%d; HttpOnly", COOKIE_NAME, request->config->httpdPort, request->uuid, request->config->rootapi, request->config->cntxTimeout);
		MHD_add_response_header(webResponse, MHD_HTTP_HEADER_SET_COOKIE, cookie);
	}
	// if requested add an error status
	if (request->errcode != 0)
		ret = MHD_queue_response(connection, request->errcode, webResponse);
	else
		MHD_queue_response(connection, MHD_HTTP_OK, webResponse);

	MHD_destroy_response(webResponse);
	json_object_put(request->jresp);	// decrease reference rqtcount to free the json object
	freeRequest(request);
	return MHD_YES;
}

int doRestApi(struct MHD_Connection *connection, AFB_session * session, const char *url, const char *method, const char *upload_data, size_t * upload_data_size, void **con_cls)
{
	int rc;

	if (afbJsonType == NULL)
		afbJsonType = json_object_new_string (AFB_MSG_JTYPE);

	if (0 == strcmp(method, MHD_HTTP_METHOD_POST)) {
		rc = doRestApiPost(connection, session, url, method, upload_data, upload_data_size, con_cls);
	} else {
		rc = doRestApiGet(connection, session, url, method, upload_data, upload_data_size, con_cls);
	}
	return rc;
}

