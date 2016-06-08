/*
 * Copyright (C) 2016 "IoT.bzh"
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

#include <json-c/json.h>

#include "afb-msg-json.h"
#include "afb-context.h"


struct json_object *afb_msg_json_reply(const char *status, const char *info, struct json_object *resp, struct afb_context *context, const char *reqid)
{
	json_object *msg, *request;
	const char *token, *uuid;
	static json_object *type_reply = NULL;

	msg = json_object_new_object();
	if (resp != NULL)
		json_object_object_add(msg, "response", resp);

	if (type_reply == NULL)
		type_reply = json_object_new_string("afb-reply");
	json_object_object_add(msg, "jtype", json_object_get(type_reply));

	request = json_object_new_object();
	json_object_object_add(msg, "request", request);
	json_object_object_add(request, "status", json_object_new_string(status));

	if (info != NULL)
		json_object_object_add(request, "info", json_object_new_string(info));

	if (reqid != NULL)
		json_object_object_add(request, "reqid", json_object_new_string(reqid));

	token = afb_context_sent_token(context);
	if (token != NULL)
		json_object_object_add(request, "token", json_object_new_string(token));

	uuid = afb_context_sent_uuid(context);
	if (uuid != NULL)
		json_object_object_add(request, "uuid", json_object_new_string(uuid));

	return msg;
}

struct json_object *afb_msg_json_reply_ok(const char *info, struct json_object *resp, struct afb_context *context, const char *reqid)
{
	return afb_msg_json_reply("success", info, resp, context, reqid);
}

struct json_object *afb_msg_json_reply_error(const char *status, const char *info, struct afb_context *context, const char *reqid)
{
	return afb_msg_json_reply(status, info, NULL, context, reqid);
}

struct json_object *afb_msg_json_event(const char *event, struct json_object *object)
{
	json_object *msg;
	static json_object *type_event = NULL;

	msg = json_object_new_object();

	json_object_object_add(msg, "event", json_object_new_string(event));

	if (object != NULL)
		json_object_object_add(msg, "data", object);

	if (type_event == NULL)
		type_event = json_object_new_string("afb-event");
	json_object_object_add(msg, "jtype", json_object_get(type_event));

	return msg;
}

