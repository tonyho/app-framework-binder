/*
 * Copyright (C) 2015 "IoT.bzh"
 * Author "Fulup Ar Foll"
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
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <string.h>
#include <json.h>

#include "afb-plugin.h"
#include "afb-req-itf.h"

#include "utils-jbus.h"

static const char _auto_[]      = "auto";
static const char _continue_[]  = "continue";
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

static const struct AFB_interface *interface;

static struct jbus *jbus;

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

static void embed_call_void(struct afb_req request, const char *method)
{
	struct json_object *obj = jbus_call_sj_sync(jbus, method, "true");
	if (interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) %s(true) -> %s\n", method, obj ? json_object_to_json_string(obj) : "NULL");
	if (obj == NULL) {
		afb_req_fail(request, "failed", "framework daemon failure");
		return;
	}
	obj = embed(method, obj);
	if (obj == NULL) {
		afb_req_fail(request, "failed", "framework daemon failure");
		return;
	}
	afb_req_success(request, obj, NULL);
}

static void call_appid(struct afb_req request, const char *method)
{
	struct json_object *obj;
	char *sid;
	const char *id = afb_req_argument(request, _id_);
	if (id == NULL) {
		afb_req_fail(request, "bad-request", "missing 'id'");
		return;
	}
	if (asprintf(&sid, "\"%s\"", id) <= 0) {
		afb_req_fail(request, "server-error", "out of memory");
		return;
	}
	obj = jbus_call_sj_sync(jbus, method, sid);
	if (interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) %s(%s) -> %s\n", method, sid, obj ? json_object_to_json_string(obj) : "NULL");
	free(sid);
	if (obj == NULL) {
		afb_req_fail(request, "failed", "framework daemon failure");
		return;
	}
	afb_req_success(request, obj, NULL);
}

static void call_runid(struct afb_req request, const char *method)
{
	struct json_object *obj;
	const char *id = afb_req_argument(request, _runid_);
	if (id == NULL) {
		afb_req_fail(request, "bad-request", "missing 'runid'");
		return;
	}
	obj = jbus_call_sj_sync(jbus, method, id);
	if (interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) %s(%s) -> %s\n", method, id,
				obj ? json_object_to_json_string(obj) : "NULL");
	if (obj == NULL) {
		afb_req_fail(request, "failed", "framework daemon failure");
		return;
	}
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
	id = afb_req_argument(request, _id_);
	if (id == NULL) {
		afb_req_fail(request, "bad-request", "missing 'id'");
		return;
	}
	/* get the mode */
	mode = afb_req_argument(request, _mode_);
	if (mode == NULL || !strcmp(mode, _auto_)) {
		mode = interface->mode == AFB_MODE_REMOTE ? _remote_ : _local_;
	}

	/* create the query */
	rc = asprintf(&query, "{\"id\":\"%s\",\"mode\":\"%s\"}", id, mode);
	if (rc < 0) {
		afb_req_fail(request, "server-error", "out of memory");
		return;
	}

	/* calls the service */
	obj = jbus_call_sj_sync(jbus, _start_, query);
	if (interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) start(%s) -> %s\n", query, obj ? json_object_to_json_string(obj) : "NULL");
	free(query);

	/* check status */
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
	struct json_object *obj;
	char *query;
	const char *filename;
	struct afb_arg arg;

	/* get the argument */
	arg = afb_req_get(request, "widget");
	filename = arg.value;
	if (filename == NULL || !arg.is_file) {
		afb_req_fail(request, "bad-request", "missing 'widget' file");
		return;
	}

	/* makes the query */
	if (0 >= asprintf(&query, "\"%s\"", filename)) {
		afb_req_fail(request, "server-error", "out of memory");
		return;
	}

	obj = jbus_call_sj_sync(jbus, _install_, query);
	if (interface->verbosity)
		fprintf(stderr, "(afm-main-plugin) install(%s) -> %s\n", query, obj ? json_object_to_json_string(obj) : "NULL");
	free(query);

	/* check status */
	if (obj == NULL) {
		afb_req_fail(request, "failed", "framework daemon failure");
		return;
	}

	/* embed if needed */
	obj = embed(_id_, obj);
	afb_req_success(request, obj, NULL);
}

static void uninstall(struct afb_req request)
{
	call_appid(request, _uninstall_);
}

static const struct AFB_restapi plug_apis[] =
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
	.type = AFB_PLUGIN_JSON,
	.info = "Application Framework Master Service",
	.prefix = "afm-main",
	.apis = plug_apis
};

const struct AFB_plugin *pluginRegister(const struct AFB_interface *itf)
{
	interface = itf;

	jbus = create_jbus_session("/org/AGL/afm/user");
        if (jbus)
		return &plug_desc;
	fprintf(stderr, "ERROR: %s:%d: can't connect to DBUS session\n", __FILE__, __LINE__);
	return NULL;
}

