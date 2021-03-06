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

#pragma once

struct json_object;
struct afb_context;
struct afb_arg;

extern struct json_object *afb_msg_json_reply(const char *status, const char *info, struct json_object *resp, struct afb_context *context, const char *reqid);
extern struct json_object *afb_msg_json_reply_ok(const char *info, struct json_object *resp, struct afb_context *context, const char *reqid);
extern struct json_object *afb_msg_json_reply_error(const char *status, const char *info, struct afb_context *context, const char *reqid);

extern struct json_object *afb_msg_json_event(const char *event, struct json_object *object);

extern struct afb_arg afb_msg_json_get_arg(struct json_object *object, const char *name);

