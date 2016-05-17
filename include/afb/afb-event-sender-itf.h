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

struct afb_event_sender_itf {
	void (*push)(void *event_sender, const char *name, struct json_object *object);
};

struct afb_event_sender {
	const struct afb_event_sender_itf *itf;
	void *closure;
};

static inline void afb_event_sender_push(struct afb_event_sender mgr, const char *name, struct json_object *object)
{
	return mgr.itf->push(mgr.closure, name, object);
}

