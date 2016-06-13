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

/* avoid inclusion of <json-c/json.h> */
struct json_object;

struct afb_service_itf
{
	void (*call)(void *closure, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *callback_closure);
};

struct afb_service
{
	const struct afb_service_itf *itf;
	void *closure;
};

extern int pluginAfbV1ServiceInit(struct afb_service service);

extern void pluginAfbV1ServiceEvent(const char *event, struct json_object *object);

static inline void afb_service_call(struct afb_service service, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *callback_closure)
{
	service.itf->call(service.closure, api, verb, args, callback, callback_closure);
}

