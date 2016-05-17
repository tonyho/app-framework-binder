/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
 * Author "Fulup Ar Foll"
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

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <json-c/json.h>

#include <afb/afb-plugin.h>

#include "utils-jbus.h"

static const char _added_[]     = "added";
static const char _auto_[]      = "auto";
static const char _continue_[]  = "continue";
static const char _changed_[]   = "changed";
static const char _detail_[]    = "detail";
static const char _id_[]        = "id";
static const char _install_[]   = "install";
static const char _local_[]     = "local";
static const char _mode_[]      = "mode";
static const char _remote_[]    = "remote";
static const char _runid_[]     = "runid";
static const char _runnables_[] = "runnables";
static const char _runners_[]   = "runners";
static const char _start_[]     = "start";
static const char _state_[]     = "state";
static const char _stop_[]      = "stop";
static const char _terminate_[] = "terminate";
static const char _uninstall_[] = "uninstall";
static const char _uri_[]       = "uri";

static const struct AFB_interface *afb_interface;
static struct afb_evmgr evmgr;

static struct jbus *jbus;

struct memo
{
	struct afb_req request;
	const char *method;
};

static struct memo *make_memo(struct afb_req request, const char *method)
{
	struct memo *memo = malloc(sizeof *memo);
	if (memo != NULL) {
		memo->request = request;
		memo->method = method;
		afb_req_addref(request);
	}
	return memo;
}

static void application_list_changed(const char *data, void *closure)
{
	afb_evmgr_push(evmgr, "application-list-changed", NULL);
}

static struct json_object *embed(const char *tag, struct json_object *obj)
{
	struct json_object *result;

	if (obj == NULL)
		result = NULL;
	else if (!tag)
		result = obj;
	else {
		result = json_object_new_object();
		if (result == NULL) {
			/* can't embed */
			result = obj;
		}
		else {
			/* TODO why is json-c not returning a status? */
			json_object_object_add(result, tag, obj);
		}
	}
	return result;
}

static void embed_call_void_callback(int status, struct json_object *obj, struct memo *memo)
{
	if (afb_interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) %s(true) -> %s\n", memo->method,
			obj ? json_object_to_json_string(obj) : "NULL");
	if (obj == NULL) {
		afb_req_fail(memo->request, "failed", "framework daemon failure");
	} else {
		obj = json_object_get(obj);
		obj = embed(memo->method, obj);
		if (obj == NULL) {
			afb_req_fail(memo->request, "failed", "framework daemon failure");
		} else {
			afb_req_success(memo->request, obj, NULL);
		}
	}
	afb_req_unref(memo->request);
	free(memo);
}

static void embed_call_void(struct afb_req request, const char *method)
{
	struct memo *memo = make_memo(request, method);
	if (memo == NULL)
		afb_req_fail(request, "failed", "out of memory");
	else if (jbus_call_sj(jbus, method, "true", (void*)embed_call_void_callback, memo) < 0) {
		afb_req_fail(request, "failed", "dbus failure");
		free(memo);
	}
}

static void call_appid_callback(int status, struct json_object *obj, struct memo *memo)
{
	if (afb_interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) %s -> %s\n", memo->method, 
			obj ? json_object_to_json_string(obj) : "NULL");
	if (obj == NULL) {
		afb_req_fail(memo->request, "failed", "framework daemon failure");
	} else {
		obj = json_object_get(obj);
		afb_req_success(memo->request, obj, NULL);
	}
	afb_req_unref(memo->request);
	free(memo);
}

static void call_appid(struct afb_req request, const char *method)
{
	struct memo *memo;
	char *sid;
	const char *id = afb_req_value(request, _id_);
	if (id == NULL) {
		afb_req_fail(request, "bad-request", "missing 'id'");
		return;
	}
	memo = make_memo(request, method);
	if (asprintf(&sid, "\"%s\"", id) <= 0 || memo == NULL) {
		afb_req_fail(request, "server-error", "out of memory");
		free(memo);
		return;
	}
	if (jbus_call_sj(jbus, method, sid, (void*)call_appid_callback, memo) < 0) {
		afb_req_fail(request, "failed", "dbus failure");
		free(memo);
	}
	free(sid);
}

static void call_runid(struct afb_req request, const char *method)
{
	struct json_object *obj;
	const char *id = afb_req_value(request, _runid_);
	if (id == NULL) {
		afb_req_fail(request, "bad-request", "missing 'runid'");
		return;
	}
	obj = jbus_call_sj_sync(jbus, method, id);
	if (afb_interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) %s(%s) -> %s\n", method, id,
				obj ? json_object_to_json_string(obj) : "NULL");
	if (obj == NULL) {
		afb_req_fail(request, "failed", "framework daemon failure");
		return;
	}
	obj = json_object_get(obj);
	afb_req_success(request, obj, NULL);
}

/************************** entries ******************************/

static void runnables(struct afb_req request)
{
	embed_call_void(request, _runnables_);
}

static void detail(struct afb_req request)
{
	call_appid(request, _detail_);
}

static void start(struct afb_req request)
{
	struct json_object *obj;
	const char *id, *mode;
	char *query;
	int rc;

	/* get the id */
	id = afb_req_value(request, _id_);
	if (id == NULL) {
		afb_req_fail(request, "bad-request", "missing 'id'");
		return;
	}
	/* get the mode */
	mode = afb_req_value(request, _mode_);
	if (mode == NULL || !strcmp(mode, _auto_)) {
		mode = afb_interface->mode == AFB_MODE_REMOTE ? _remote_ : _local_;
	}

	/* create the query */
	rc = asprintf(&query, "{\"id\":\"%s\",\"mode\":\"%s\"}", id, mode);
	if (rc < 0) {
		afb_req_fail(request, "server-error", "out of memory");
		return;
	}

	/* calls the service */
	obj = jbus_call_sj_sync(jbus, _start_, query);
	if (afb_interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) start(%s) -> %s\n", query,
			obj ? json_object_to_json_string(obj) : "NULL");
	free(query);

	/* check status */
	obj = json_object_get(obj);
	if (obj == NULL) {
		afb_req_fail(request, "failed", "framework daemon failure");
		return;
	}

	/* embed if needed */
	if (json_object_get_type(obj) == json_type_int)
		obj = embed(_runid_, obj);
	afb_req_success(request, obj, NULL);
}

static void terminate(struct afb_req request)
{
	call_runid(request, _terminate_);
}

static void stop(struct afb_req request)
{
	call_runid(request, _stop_);
}

static void continue_(struct afb_req request)
{
	call_runid(request, _continue_);
}

static void runners(struct afb_req request)
{
	embed_call_void(request, _runners_);
}

static void state(struct afb_req request)
{
	call_runid(request, _state_);
}

static void install(struct afb_req request)
{
	struct json_object *obj, *added;
	char *query;
	const char *filename;
	struct afb_arg arg;

	/* get the argument */
	arg = afb_req_get(request, "widget");
	filename = arg.path;
	if (filename == NULL) {
		afb_req_fail(request, "bad-request", "missing 'widget' file");
		return;
	}

	/* makes the query */
	if (0 >= asprintf(&query, "\"%s\"", filename)) {
		afb_req_fail(request, "server-error", "out of memory");
		return;
	}

	obj = jbus_call_sj_sync(jbus, _install_, query);
	if (afb_interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) install(%s) -> %s\n", query,
			obj ? json_object_to_json_string(obj) : "NULL");
	free(query);

	/* check status */
	if (obj == NULL) {
		afb_req_fail(request, "failed", "framework daemon failure");
		return;
	}

	/* embed if needed */
	if (json_object_object_get_ex(obj, _added_, &added))
		obj = added;
	obj = json_object_get(obj);
	obj = embed(_id_, obj);
	afb_req_success(request, obj, NULL);
}

static void uninstall(struct afb_req request)
{
	call_appid(request, _uninstall_);
}

static const struct AFB_verb_desc_v1 verbs[] =
{
	{_runnables_, AFB_SESSION_CHECK, runnables,  "Get list of runnable applications"},
	{_detail_   , AFB_SESSION_CHECK, detail, "Get the details for one application"},
	{_start_    , AFB_SESSION_CHECK, start, "Start an application"},
	{_terminate_, AFB_SESSION_CHECK, terminate, "Terminate a running application"},
	{_stop_     , AFB_SESSION_CHECK, stop, "Stop (pause) a running application"},
	{_continue_ , AFB_SESSION_CHECK, continue_, "Continue (resume) a stopped application"},
	{_runners_  , AFB_SESSION_CHECK, runners,  "Get the list of running applications"},
	{_state_    , AFB_SESSION_CHECK, state, "Get the state of a running application"},
	{_install_  , AFB_SESSION_CHECK, install,  "Install an application using a widget file"},
	{_uninstall_, AFB_SESSION_CHECK, uninstall, "Uninstall an application"},
	{ NULL, 0, NULL, NULL }
};

static const struct AFB_plugin plug_desc = {
	.type = AFB_PLUGIN_VERSION_1,
	.v1 = {
		.info = "Application Framework Master Service",
		.prefix = "afm-main",
		.verbs = verbs
	}
};

const struct AFB_plugin *pluginAfbV1Register(const struct AFB_interface *itf)
{
	int rc;
	struct sd_bus *sbus;

	/* records the interface */
	assert (afb_interface == NULL);
	afb_interface = itf;
	evmgr = afb_daemon_get_evmgr(itf->daemon);

	/* creates the jbus for accessing afm-user-daemon */
	sbus = afb_daemon_get_user_bus(itf->daemon);
	if (sbus == NULL)
		return NULL;
	jbus = create_jbus(sbus, "/org/AGL/afm/user");
        if (jbus == NULL)
		return NULL;

	/* records the signal handler */
	rc = jbus_on_signal_s(jbus, _changed_, application_list_changed, NULL);
	if (rc < 0) {
		jbus_unref(jbus);
		return NULL;
	}

	return &plug_desc;
}

