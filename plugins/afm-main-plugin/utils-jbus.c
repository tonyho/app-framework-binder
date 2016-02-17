/*
 Copyright 2015 IoT.bzh

 author: José Bollo <jose.bollo@iot.bzh>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <assert.h>

#include <json.h>
#include <dbus/dbus.h>

#include "utils-jbus.h"

struct jreq;
struct jservice;
struct jbus;

/* structure for handled requests */
struct jreq {
	DBusConnection *connection;
	DBusMessage *request;
};

/* structure for recorded services */
struct jservice {
	struct jservice *next;
	char *method;
	void (*oncall_s)(struct jreq *, const char *);
	void (*oncall_j)(struct jreq *, struct json_object *);
};

/* structure for signal handlers */
struct jsignal {
	struct jsignal *next;
	char *name;
	void (*onsignal_s)(const char *);
	void (*onsignal_j)(struct json_object *);
};

/* structure for recording asynchronous requests */
struct jrespw {
	struct jrespw *next;
	dbus_uint32_t serial;
	void *data;
	void (*onresp_s)(int, const char*, void *);
	void (*onresp_j)(int, struct json_object*, void *);
};

/* structure for synchronous requests */
struct respsync {
	int replied;
	char *value;
};

/* structure for handling either client or server jbus on dbus */
struct jbus {
	int refcount;
	struct jservice *services;
	DBusConnection *connection;
	struct jsignal *signals;
	struct jrespw *waiters;
	char *path;
	char *name;
	int watchnr;
	int watchfd;
	int watchflags;
};

/*********************** STATIC COMMON METHODS *****************/

static inline void free_jreq(struct jreq *jreq)
{
	dbus_message_unref(jreq->request);
	dbus_connection_unref(jreq->connection);
	free(jreq);
}

static inline int reply_out_of_memory(struct jreq *jreq)
{
	static const char out_of_memory[] = "out of memory";
	jbus_reply_error_s(jreq, out_of_memory);
	errno = ENOMEM;
	return -1;
}

static inline int reply_invalid_request(struct jreq *jreq)
{
	static const char invalid_request[] = "invalid request";
	jbus_reply_error_s(jreq, invalid_request);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static int matchitf(struct jbus *jbus, DBusMessage *message)
{
	const char *itf = dbus_message_get_interface(message);
	return itf != NULL && !strcmp(itf, jbus->name);
}

static int add_service(
		struct jbus *jbus,
		const char *method,
		void (*oncall_s)(struct jreq*, const char*),
		void (*oncall_j)(struct jreq*, struct json_object*)
)
{
	struct jservice *srv;

	/* allocation */
	srv = malloc(sizeof * srv);
	if (srv == NULL) {
		errno = ENOMEM;
		goto error;
	}
	srv->method = strdup(method);
	if (!srv->method) {
		errno = ENOMEM;
		goto error2;
	}

	/* record the service */
	srv->oncall_s = oncall_s;
	srv->oncall_j = oncall_j;
	srv->next = jbus->services;
	jbus->services = srv;

	return 0;

error2:
	free(srv);
error:
	return -1;
}

static int add_signal(
	struct jbus *jbus,
	const char *name,
	void (*onsignal_s)(const char*),
	void (*onsignal_j)(struct json_object*)
)
{
	char *rule;
	struct jsignal *sig;

	/* record the signal */
	if (jbus->signals == NULL) {
#if 0
		if (0 >= asprintf(&rule, "type='signal',interface='%s',path='%s'", jbus->name, jbus->path))
#else
		if (0 >= asprintf(&rule, "type='signal',sender='%s',interface='%s',path='%s'", jbus->name, jbus->name, jbus->path))
#endif
			return -1;
		dbus_bus_add_match(jbus->connection, rule, NULL);
		free(rule);
	}

	/* allocation */
	sig = malloc(sizeof * sig);
	if (sig == NULL)
		goto error;
	sig->name = strdup(name);
	if (!sig->name)
		goto error2;

	/* record the signal */
	sig->onsignal_s = onsignal_s;
	sig->onsignal_j = onsignal_j;
	sig->next = jbus->signals;
	jbus->signals = sig;

	return 0;

error2:
	free(sig);
error:
	errno = ENOMEM;
	return -1;
}

static int call(
	struct jbus *jbus,
	const char *method,
	const char *query,
	void (*onresp_s)(int status, const char *response, void *data),
	void (*onresp_j)(int status, struct json_object *response, void *data),
	void *data
)
{
	DBusMessage *msg;
	struct jrespw *resp;

	resp = malloc(sizeof * resp);
	if (resp == NULL) {
		errno = ENOMEM;
		goto error;
	}

	msg = dbus_message_new_method_call(jbus->name, jbus->path, jbus->name, method);
	if (msg == NULL) {
		errno = ENOMEM;
		goto error2;
	}

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &query, DBUS_TYPE_INVALID)) {
		errno = ENOMEM;
		goto error3;
	}

	if (!dbus_connection_send(jbus->connection, msg, &resp->serial)) {
		goto error3;
	}

	dbus_message_unref(msg);
	resp->data = data;
	resp->onresp_s = onresp_s;
	resp->onresp_j = onresp_j;
	resp->next = jbus->waiters;
	jbus->waiters = resp;
	return 0;

error3:
	dbus_message_unref(msg);
error2:
	free(resp);
error:
	return -1;
}

static void sync_of_replies(int status, const char *value, void *data)
{
	struct respsync *s = data;
	s->value = status ? NULL : strdup(value ? value : "");
	s->replied = 1;
}

static DBusHandlerResult incoming_resp(DBusConnection *connection, DBusMessage *message, struct jbus *jbus, int iserror)
{
	int status;
	const char *str;
	struct jrespw *jrw, **prv;
	struct json_object *reply;
	dbus_uint32_t serial;

	/* search for the waiter */
	serial = dbus_message_get_reply_serial(message);
	prv = &jbus->waiters;
	while ((jrw = *prv) != NULL && jrw->serial != serial)
		prv = &jrw->next;
	if (jrw == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	*prv = jrw->next;

	/* retrieve the string value */
	if (dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &str, DBUS_TYPE_INVALID))
		status = 0;
	else {
		status = -1;
		str = NULL;
		reply = NULL;
	}

	/* treat it */
	if (jrw->onresp_s)
		jrw->onresp_s(iserror ? -1 : status, str, jrw->data);
	else {
		reply = json_tokener_parse(str);
		status = reply ? 0 : -1;
		jrw->onresp_j(iserror ? -1 : status, reply, jrw->data);
		json_object_put(reply);
	}

	free(jrw);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult incoming_call(DBusConnection *connection, DBusMessage *message, struct jbus *jbus)
{
	struct jservice *srv;
	struct jreq *jreq;
	const char *str;
	const char *method;
	struct json_object *query;

	/* search for the service */
	if (!matchitf(jbus, message))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	method = dbus_message_get_member(message);
	if (method == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	srv = jbus->services;
	while(srv != NULL && strcmp(method, srv->method))
		srv = srv->next;
	if (srv == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	/* handle the message */
	jreq = malloc(sizeof * jreq);
	if (jreq == NULL)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	jreq->request = dbus_message_ref(message);
	jreq->connection = dbus_connection_ref(jbus->connection);
	
	/* retrieve the string value */
	if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &str, DBUS_TYPE_INVALID))
		return reply_invalid_request(jreq);
	if (srv->oncall_s) {
		/* handling strings only */
		srv->oncall_s(jreq, str);
	}
	else {
		/* handling json only */
		query = json_tokener_parse(str);
		if (query == NULL)
			return reply_invalid_request(jreq);
		srv->oncall_j(jreq, query);
		json_object_put(query);
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult incoming_signal(DBusConnection *connection, DBusMessage *message, struct jbus *jbus)
{
	struct jsignal *sig;
	const char *str;
	const char *name;
	struct json_object *obj;

	/* search for the service */
	if (!matchitf(jbus, message))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	name = dbus_message_get_member(message);
	if (name == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	sig = jbus->signals;
	while(sig != NULL && strcmp(name, sig->name))
		sig = sig->next;
	if (sig == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	/* retrieve the string value */
	if (dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &str, DBUS_TYPE_INVALID)) {
		if (sig->onsignal_s) {
			/* handling strings only */
			sig->onsignal_s(str);
		}
		else {
			/* handling json only */
			obj = json_tokener_parse(str);
			if (obj != NULL) {
				sig->onsignal_j(obj);
				json_object_put(obj);
			}
		}
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult incoming(DBusConnection *connection, DBusMessage *message, void *data)
{
	switch(dbus_message_get_type(message)) {
	case DBUS_MESSAGE_TYPE_METHOD_CALL:
		return incoming_call(connection, message, (struct jbus*)data);
	case DBUS_MESSAGE_TYPE_METHOD_RETURN:
		return incoming_resp(connection, message, (struct jbus*)data, 0);
	case DBUS_MESSAGE_TYPE_ERROR:
		return incoming_resp(connection, message, (struct jbus*)data, 1);
	case DBUS_MESSAGE_TYPE_SIGNAL:
		return incoming_signal(connection, message, (struct jbus*)data);
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void watchset(DBusWatch *watch, struct jbus *jbus)
{
	unsigned int flags;
	int wf, e;

	flags = dbus_watch_get_flags(watch);
	e = dbus_watch_get_enabled(watch);
	wf = jbus->watchflags;
	if (e) {
		if (flags & DBUS_WATCH_READABLE)
			wf |= POLLIN;
		if (flags & DBUS_WATCH_WRITABLE)
			wf |= POLLOUT;
	}
	else {
		if (flags & DBUS_WATCH_READABLE)
			wf &= ~POLLIN;
		if (flags & DBUS_WATCH_WRITABLE)
			wf &= ~POLLOUT;
	}
	jbus->watchflags = wf;
}

static void watchdel(DBusWatch *watch, void *data)
{
	struct jbus *jbus = data;

	assert(jbus->watchnr > 0);
	assert(jbus->watchfd == dbus_watch_get_unix_fd(watch));
	jbus->watchnr--;
}

static void watchtoggle(DBusWatch *watch, void *data)
{
	struct jbus *jbus = data;

	assert(jbus->watchnr > 0);
	assert(jbus->watchfd == dbus_watch_get_unix_fd(watch));
	watchset(watch, jbus);
}

static dbus_bool_t watchadd(DBusWatch *watch, void *data)
{
	struct jbus *jbus = data;
	if (jbus->watchnr == 0) {
		jbus->watchfd = dbus_watch_get_unix_fd(watch);
		jbus->watchflags = 0;
	}
	else if (jbus->watchfd != dbus_watch_get_unix_fd(watch))
		return FALSE;
	jbus->watchnr++;
	watchset(watch, jbus);
	return TRUE;
}

/************************** MAIN FUNCTIONS *****************************************/

struct jbus *create_jbus(int session, const char *path)
{
	struct jbus *jbus;
	char *name;

	/* create the context and connect */
	jbus = calloc(1, sizeof * jbus);
	if (jbus == NULL) {
		errno = ENOMEM;
		goto error;
	}
	jbus->refcount = 1;
	jbus->path = strdup(path);
	if (jbus->path == NULL) {
		errno = ENOMEM;
		goto error2;
	}
	while(*path == '/') path++;
	jbus->name = name = strdup(path);
	if (name == NULL) {
		errno = ENOMEM;
		goto error2;
	}
	while(*name) {
		if (*name == '/')
			*name = '.';
		name++;
	}
	name--;
	while (name >= jbus->name && *name == '.')
		*name-- = 0;
	if (!*jbus->name) {
		errno = EINVAL;
		goto error2;
	}

	/* connect */
	jbus->connection = dbus_bus_get(session ? DBUS_BUS_SESSION : DBUS_BUS_SYSTEM, NULL);
	if (jbus->connection == NULL
	|| !dbus_connection_add_filter(jbus->connection, incoming, jbus, NULL)
        || !dbus_connection_set_watch_functions(jbus->connection, watchadd, watchdel, watchtoggle, jbus, NULL))
		goto error2;

	return jbus;

error2:
	jbus_unref(jbus);
error:
	return NULL;
}

void jbus_addref(struct jbus *jbus)
{
	jbus->refcount++;
}

void jbus_unref(struct jbus *jbus)
{
	struct jservice *srv;
	if (!--jbus->refcount) {
		if (jbus->connection != NULL)
			dbus_connection_unref(jbus->connection);
		while((srv = jbus->services) != NULL) {
			jbus->services = srv->next;
			free(srv->method);
			free(srv);
		}
		free(jbus->name);
		free(jbus->path);
		free(jbus);
	}
}

int jbus_reply_error_s(struct jreq *jreq, const char *error)
{
	int rc = -1;
	DBusMessage *message;

	message = dbus_message_new_error(jreq->request, DBUS_ERROR_FAILED, error);
	if (message == NULL)
		errno = ENOMEM;
	else {
		if (dbus_connection_send(jreq->connection, message, NULL))
			rc = 0;
		dbus_message_unref(message);
	}
	free_jreq(jreq);
	return rc;
}

int jbus_reply_error_j(struct jreq *jreq, struct json_object *reply)
{
	const char *str = json_object_to_json_string(reply);
	return str ? jbus_reply_error_s(jreq, str) : reply_out_of_memory(jreq);
}

int jbus_reply_s(struct jreq *jreq, const char *reply)
{
	int rc = -1;
	DBusMessage *message;

	message = dbus_message_new_method_return(jreq->request);
	if (message == NULL)
		return reply_out_of_memory(jreq);

	if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &reply, DBUS_TYPE_INVALID)) {
		dbus_message_unref(message);
		return reply_out_of_memory(jreq);
	}

	if (dbus_connection_send(jreq->connection, message, NULL))
		rc = 0;
	dbus_message_unref(message);
	free_jreq(jreq);
	return rc;
}

int jbus_reply_j(struct jreq *jreq, struct json_object *reply)
{
	const char *str = json_object_to_json_string(reply);
	return str ? jbus_reply_s(jreq, str) : reply_out_of_memory(jreq);
}

int jbus_send_signal_s(struct jbus *jbus, const char *name, const char *content)
{
	int rc = -1;
	DBusMessage *message;

	message = dbus_message_new_signal(jbus->path, jbus->name, name);
	if (message == NULL)
		goto error;

	if (!dbus_message_set_sender(message, jbus->name)
	||  !dbus_message_append_args(message, DBUS_TYPE_STRING, &content, DBUS_TYPE_INVALID)) {
		dbus_message_unref(message);
		goto error;
	}

	if (dbus_connection_send(jbus->connection, message, NULL))
		rc = 0;
	dbus_message_unref(message);
	return rc;

error:
	errno = ENOMEM;
	return -1;
}

int jbus_send_signal_j(struct jbus *jbus, const char *name, struct json_object *content)
{
	const char *str = json_object_to_json_string(content);
	if (str == NULL) {
		errno = ENOMEM;
		return -1;
	}
	return jbus_send_signal_s(jbus, name, str);
}

int jbus_add_service_s(struct jbus *jbus, const char *method, void (*oncall)(struct jreq *, const char *))
{
	return add_service(jbus, method, oncall, NULL);
}

int jbus_add_service_j(struct jbus *jbus, const char *method, void (*oncall)(struct jreq *, struct json_object *))
{
	return add_service(jbus, method, NULL, oncall);
}

int jbus_start_serving(struct jbus *jbus)
{
	int status = dbus_bus_request_name(jbus->connection, jbus->name, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL);
	switch (status) {
	case DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER:
	case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
		return 0;
	case DBUS_REQUEST_NAME_REPLY_EXISTS:
	case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
	default:
		errno = EADDRINUSE;
		return -1;
	}
}

int jbus_fill_pollfds(struct jbus **jbuses, int njbuses, struct pollfd *fds)
{
	int i, r;

	for (r = i = 0 ; i < njbuses ; i++) {
		if (jbuses[i]->watchnr) {
			fds[r].fd = jbuses[i]->watchfd;
			fds[r].events = jbuses[i]->watchflags;
			r++;
		}
	}
	return r;
}

int jbus_dispatch_pollfds(struct jbus **jbuses, int njbuses, struct pollfd *fds, int maxcount)
{
	int i, r, n;
	DBusDispatchStatus sts;

	for (r = n = i = 0 ; i < njbuses && n < maxcount ; i++) {
		if (jbuses[i]->watchnr && fds[r].fd == jbuses[i]->watchfd) {
			if (fds[r].revents) {
				dbus_connection_read_write(jbuses[i]->connection, 0);
				sts = dbus_connection_get_dispatch_status(jbuses[i]->connection);
				while(sts == DBUS_DISPATCH_DATA_REMAINS &&  n < maxcount) {
					sts = dbus_connection_dispatch(jbuses[i]->connection);
					n++;
				}
			}
			r++;
		}
	}
	return n;
}

int jbus_dispatch_multiple(struct jbus **jbuses, int njbuses, int maxcount)
{
	int i, r;
	DBusDispatchStatus sts;

	for (i = r = 0 ; i < njbuses && r < maxcount ; i++) {
		dbus_connection_read_write(jbuses[i]->connection, 0);
		sts = dbus_connection_get_dispatch_status(jbuses[i]->connection);
		while(sts == DBUS_DISPATCH_DATA_REMAINS &&  r < maxcount) {
			sts = dbus_connection_dispatch(jbuses[i]->connection);
			r++;
		}
	}
	return r;
}

int jbus_read_write_dispatch_multiple(struct jbus **jbuses, int njbuses, int toms, int maxcount)
{
	int n, r, s;
	struct pollfd *fds;

	if (njbuses < 0 || njbuses > 100) {
		errno = EINVAL;
		return -1;
	}
	fds = alloca(njbuses * sizeof * fds);
	assert(fds != NULL);

	r = jbus_dispatch_multiple(jbuses, njbuses, maxcount);
	if (r)
		return r;
	n = jbus_fill_pollfds(jbuses, njbuses, fds);
	for(;;) {
		s = poll(fds, n, toms);
		if (s >= 0)
			break;
		if (errno != EINTR)
			return r ? r : s;
		toms = 0;
	}
	n = jbus_dispatch_pollfds(jbuses, njbuses, fds, maxcount - r);
	return n >= 0 ? r + n : r ? r : n;
}

int jbus_read_write_dispatch(struct jbus *jbus, int toms)
{
	int r = jbus_read_write_dispatch_multiple(&jbus, 1, toms, 1000);
	return r < 0 ? r : 0;
}

int jbus_call_ss(struct jbus *jbus, const char *method, const char *query, void (*onresp)(int, const char*, void*), void *data)
{
	return call(jbus, method, query, onresp, NULL, data);
}

int jbus_call_sj(struct jbus *jbus, const char *method, const char *query, void (*onresp)(int, struct json_object*, void*), void *data)
{
	return call(jbus, method, query, NULL, onresp, data);
}

int jbus_call_js(struct jbus *jbus, const char *method, struct json_object *query, void (*onresp)(int, const char*, void*), void *data)
{
	const char *str = json_object_to_json_string(query);
	if (str == NULL) {
		errno = ENOMEM;
		return -1;
	}
	return call(jbus, method, str, onresp, NULL, data);
}

int jbus_call_jj(struct jbus *jbus, const char *method, struct json_object *query, void (*onresp)(int, struct json_object*, void*), void *data)
{
	const char *str = json_object_to_json_string(query);
	if (str == NULL) {
		errno = ENOMEM;
		return -1;
	}
	return call(jbus, method, str, NULL, onresp, data);
}

char *jbus_call_ss_sync(struct jbus *jbus, const char *method, const char *query)
{
	struct respsync synchro;
	synchro.value = NULL;
	synchro.replied = jbus_call_ss(jbus, method, query, sync_of_replies, &synchro);
	while (!synchro.replied && !jbus_read_write_dispatch(jbus, -1));
	return synchro.value;
}

struct json_object *jbus_call_sj_sync(struct jbus *jbus, const char *method, const char *query)
{
	struct json_object *obj;
	char *str = jbus_call_ss_sync(jbus, method, query);
	if (str == NULL)
		obj = NULL;
	else {
		obj = json_tokener_parse(str);
		free(str);
	}
	return obj;
}

char *jbus_call_js_sync(struct jbus *jbus, const char *method, struct json_object *query)
{
	const char *str = json_object_to_json_string(query);
	if (str == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	return jbus_call_ss_sync(jbus, method, str);
}

struct json_object *jbus_call_jj_sync(struct jbus *jbus, const char *method, struct json_object *query)
{
	const char *str = json_object_to_json_string(query);
	if (str == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	return jbus_call_sj_sync(jbus, method, str);
}

int jbus_on_signal_s(struct jbus *jbus, const char *name, void (*onsig)(const char *))
{
	return add_signal(jbus, name, onsig, NULL);
}

int jbus_on_signal_j(struct jbus *jbus, const char *name, void (*onsig)(struct json_object *))
{
	return add_signal(jbus, name, NULL, onsig);
}

/************************** FEW LITTLE TESTS *****************************************/

#ifdef SERVER
#include <stdio.h>
#include <unistd.h>
struct jbus *jbus;
void ping(struct jreq *jreq, struct json_object *request)
{
printf("ping(%s) -> %s\n",json_object_to_json_string(request),json_object_to_json_string(request));
	jbus_reply_j(jreq, request);
	json_object_put(request);	
}
void incr(struct jreq *jreq, struct json_object *request)
{
	static int counter = 0;
	struct json_object *res = json_object_new_int(++counter);
printf("incr(%s) -> %s\n",json_object_to_json_string(request),json_object_to_json_string(res));
	jbus_reply_j(jreq, res);
jbus_send_signal_j(jbus, "incremented", res);
	json_object_put(res);
	json_object_put(request);
}
int main()
{
	int s1, s2, s3;
	jbus = create_jbus(1, "/bzh/iot/jdbus");
	s1 = jbus_add_service_j(jbus, "ping", ping);
	s2 = jbus_add_service_j(jbus, "incr", incr);
	s3 = jbus_start_serving(jbus);
	printf("started %d %d %d\n", s1, s2, s3);
	while (!jbus_read_write_dispatch (jbus, -1));
}
#endif
#ifdef CLIENT
#include <stdio.h>
#include <unistd.h>
struct jbus *jbus;
void onresp(int status, struct json_object *response, void *data)
{
	printf("resp: %d, %s, %s\n",status,(char*)data,json_object_to_json_string(response));
	json_object_put(response);
}
void signaled(const char *data)
{
	printf("signaled with {%s}\n", data);
}
int main()
{
	int i = 10;
	jbus = create_jbus(1, "/bzh/iot/jdbus");
	jbus_on_signal_s(jbus, "incremented", signaled);
	while(i--) {
		jbus_call_sj(jbus, "ping", "{\"toto\":[1,2,3,4,true,\"toto\"]}", onresp, "ping");
		jbus_call_sj(jbus, "incr", "{\"doit\":\"for-me\"}", onresp, "incr");
		jbus_read_write_dispatch (jbus, 1);
	}
	printf("[[[%s]]]\n", jbus_call_ss_sync(jbus, "ping", "\"formidable!\""));
	while (!jbus_read_write_dispatch (jbus, -1));
}
#endif








