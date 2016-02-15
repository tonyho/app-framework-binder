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

#include "local-def.h"

#include "utils-jbus.h"

static const char _id_[]    = "id";
static const char _runid_[] = "runid";
static char _runnables_[]   = "runnables";
static char _detail_[]      = "detail";
static char _start_[]       = "start";
static char _terminate_[]   = "terminate";
static char _stop_[]        = "stop";
static char _continue_[]    = "continue";
static char _runners_[]     = "runners";
static char _state_[]       = "state";
static char _install_[]     = "install";
static char _uninstall_[]   = "uninstall";
static const char _mode_[]  = "mode";
static const char _local_[] = "local";
static const char _remote_[]= "remote";
static const char _auto_[]  = "auto";
static const char _uri_[]   = "uri";

static struct jbus *jbus;

static struct json_object *embed(AFB_request *request, const char *tag, struct json_object *obj)
{
	struct json_object *result;

	if (obj == NULL)
		result = NULL;
	else if (!tag) {
		request->errcode = MHD_HTTP_OK;
		result = obj;
	}
	else {
		result = json_object_new_object();
		if (result == NULL) {
			/* can't embed */
			result = obj;
			request->errcode = MHD_HTTP_INTERNAL_SERVER_ERROR;
		}
		else {
			/* TODO why is json-c not returning a status? */
			json_object_object_add(result, tag, obj);
			request->errcode = MHD_HTTP_OK;
		}
	}
	return result;
}

static struct json_object *call(AFB_request *request, AFB_PostItem *item, const char *tag, struct json_object *(*fun)(AFB_request*,AFB_PostItem*))
{
	return embed(request, tag, fun(request, item));
}

static struct json_object *call_void(AFB_request *request, AFB_PostItem *item)
{
	struct json_object *obj = jbus_call_sj_sync(jbus, request->api, "true");
	if (verbose)
		fprintf(stderr, "(afm-main-plugin) call_void: true -> %s\n", obj ? json_object_to_json_string(obj) : "NULL");
	request->errcode = obj ? MHD_HTTP_OK : MHD_HTTP_FAILED_DEPENDENCY;
	return obj;
}

static struct json_object *call_appid(AFB_request *request, AFB_PostItem *item)
{
	struct json_object *obj;
	char *sid;
	const char *id = getQueryValue(request, _id_);
	if (id == NULL) {
		request->errcode = MHD_HTTP_BAD_REQUEST;
		return NULL;
	}
	if (0 >= asprintf(&sid, "\"%s\"", id)) {
		request->errcode = MHD_HTTP_INTERNAL_SERVER_ERROR;
		return NULL;
	}
	obj = jbus_call_sj_sync(jbus, request->api, sid);
	if (verbose)
		fprintf(stderr, "(afm-main-plugin) call_appid: %s -> %s\n", sid, obj ? json_object_to_json_string(obj) : "NULL");
	free(sid);
	request->errcode = obj ? MHD_HTTP_OK : MHD_HTTP_FAILED_DEPENDENCY;
	return obj;
}

static struct json_object *call_runid(AFB_request *request, AFB_PostItem *item)
{
	struct json_object *obj;
	const char *id = getQueryValue(request, _runid_);
	if (id == NULL) {
		request->errcode = MHD_HTTP_BAD_REQUEST;
		return NULL;
	}
	obj = jbus_call_sj_sync(jbus, request->api, id);
	if (verbose)
		fprintf(stderr, "(afm-main-plugin) call_runid: %s -> %s\n", id, obj ? json_object_to_json_string(obj) : "NULL");
	request->errcode = obj ? MHD_HTTP_OK : MHD_HTTP_FAILED_DEPENDENCY;
	return obj;
}

static struct json_object *call_void__runnables(AFB_request *request, AFB_PostItem *item)
{
	return embed(request, _runnables_, call_void(request, item));
}

static struct json_object *call_start(AFB_request *request, AFB_PostItem *item)
{
	struct json_object *resp;
	const char *id, *mode;
	char *query;
	int rc;

	/* get the id */
	id = getQueryValue(request, _id_);
	if (id == NULL) {
		request->errcode = MHD_HTTP_BAD_REQUEST;
		return NULL;
	}
	/* get the mode */
	mode = getQueryValue(request, _mode_);
	if (mode == NULL || !strcmp(mode, _auto_)) {
		mode = request->config->mode == AFB_MODE_REMOTE ? _remote_ : _local_;
	}

	/* create the query */
	rc = asprintf(&query, "{\"id\":\"%s\",\"mode\":\"%s\"}", id, mode);
	if (rc < 0) {
		request->errcode = MHD_HTTP_INTERNAL_SERVER_ERROR;
		return NULL;
	}

	/* calls the service */
	resp = jbus_call_sj_sync(jbus, _start_, query);
	if (verbose)
		fprintf(stderr, "(afm-main-plugin) call_start: %s -> %s\n", query, resp ? json_object_to_json_string(resp) : "NULL");
	free(query);

	/* embed if needed */
	if (json_object_get_type(resp) == json_type_string)
		resp = embed(request, _runid_, resp);
	request->errcode = resp ? MHD_HTTP_OK : MHD_HTTP_FAILED_DEPENDENCY;
	return resp;
}

static struct json_object *call_void__runners(AFB_request *request, AFB_PostItem *item)
{
	return embed(request, _runners_, call_void(request, item));
}

static struct json_object *call_file__appid(AFB_request *request, AFB_PostItem *item)
{
	if (item == NULL) {
		const char *filename = getPostPath(request);
		if (filename != NULL) {
			struct json_object *obj;
			char *query;
			request->jresp = NULL;
			if (0 >= asprintf(&query, "\"%s\"", filename))
				request->errcode = MHD_HTTP_INTERNAL_SERVER_ERROR;
			else {
				obj = jbus_call_sj_sync(jbus, request->api, query);
				if (verbose)
					fprintf(stderr, "(afm-main-plugin) call_file_appid: %s -> %s\n", query, obj ? json_object_to_json_string(obj) : "NULL");
				free(query);
				if (obj)
					request->jresp = embed(request, _id_, obj);
				else
					request->errcode = MHD_HTTP_FAILED_DEPENDENCY;
			}
			unlink(filename);
		}
	}
	return getPostFile (request, item, "/tmp/upload");
}

static AFB_restapi plug_apis[] =
{
	{_runnables_, AFB_SESSION_CHECK, (AFB_apiCB)call_void__runnables,  "Get list of runnable applications"},
	{_detail_   , AFB_SESSION_CHECK, (AFB_apiCB)call_appid, "Get the details for one application"},
	{_start_    , AFB_SESSION_CHECK, (AFB_apiCB)call_start, "Start an application"},
	{_terminate_, AFB_SESSION_CHECK, (AFB_apiCB)call_runid, "Terminate a running application"},
	{_stop_     , AFB_SESSION_CHECK, (AFB_apiCB)call_runid, "Stop (pause) a running application"},
	{_continue_ , AFB_SESSION_CHECK, (AFB_apiCB)call_runid, "Continue (resume) a stopped application"},
	{_runners_  , AFB_SESSION_CHECK, (AFB_apiCB)call_void__runners,  "Get the list of running applications"},
	{_state_    , AFB_SESSION_CHECK, (AFB_apiCB)call_runid, "Get the state of a running application"},
	{_install_  , AFB_SESSION_CHECK, (AFB_apiCB)call_file__appid,  "Install an application using a widget file"},
	{_uninstall_, AFB_SESSION_CHECK, (AFB_apiCB)call_appid, "Uninstall an application"},
	{NULL}
};

static AFB_plugin plug_desc = {
	.type = AFB_PLUGIN_JSON,
	.info = "Application Framework Master Service",
	.prefix = "afm-main",
	.apis = plug_apis
};

AFB_plugin *pluginRegister()
{
	jbus = create_jbus(1, "/org/AGL/afm/user");
        if (jbus)
		return &plug_desc;
	fprintf(stderr, "ERROR: %s:%d: can't connect to DBUS session\n", __FILE__, __LINE__);
	return NULL;
}

