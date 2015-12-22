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


#include "local-def.h"

#include "utils-jbus.h"

static const char _id_[] = "id";
static struct jbus *jbus;

static struct json_object *call_void(AFB_request *request)
{
	struct json_object *obj = jbus_call_sj_sync(jbus, request->api, "true");
	request->errcode = obj ? MHD_HTTP_OK : MHD_HTTP_FAILED_DEPENDENCY;
	return obj;
}

static struct json_object *call_appid(AFB_request *request)
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
	free(sid);
	request->errcode = obj ? MHD_HTTP_OK : MHD_HTTP_FAILED_DEPENDENCY;
	return obj;
}

static struct json_object *call_runid(AFB_request *request)
{
	struct json_object *obj;
	const char *id = getQueryValue(request, _id_);
	if (id == NULL) {
		request->errcode = MHD_HTTP_BAD_REQUEST;
		return NULL;
	}
	obj = jbus_call_sj_sync(jbus, request->api, id);
	request->errcode = obj ? MHD_HTTP_OK : MHD_HTTP_FAILED_DEPENDENCY;
	return obj;
}

static AFB_restapi plug_apis[] =
{
	{"runnables", AFB_SESSION_CHECK, (AFB_apiCB)call_void,  "Get list of runnable applications"},
	{"detail"   , AFB_SESSION_CHECK, (AFB_apiCB)call_appid, "Get the details for one application"},
	{"start"    , AFB_SESSION_CHECK, (AFB_apiCB)call_appid, "Start an application"},
	{"terminate", AFB_SESSION_CHECK, (AFB_apiCB)call_runid, "Terminate a running application"},
	{"stop"     , AFB_SESSION_CHECK, (AFB_apiCB)call_runid, "Stop (pause) a running application"},
	{"continue" , AFB_SESSION_CHECK, (AFB_apiCB)call_runid, "Continue (resume) a stopped application"},
	{"runners"  , AFB_SESSION_CHECK, (AFB_apiCB)call_void,  "Get the list of running applications"},
	{"state"    , AFB_SESSION_CHECK, (AFB_apiCB)call_runid, "Get the state of a running application"},
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
	return jbus ? &plug_desc : NULL;
}

