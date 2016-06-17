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

struct afb_event;
struct AFB_clientCtx;

struct afb_evt_listener;

extern struct afb_evt_listener *afb_evt_listener_create(void (*send)(void *closure, const char *event, struct json_object *object), void *closure);

extern int afb_evt_broadcast(const char *event, struct json_object *object);

extern struct afb_evt_listener *afb_evt_listener_addref(struct afb_evt_listener *listener);
extern void afb_evt_listener_unref(struct afb_evt_listener *listener);

extern struct afb_event afb_evt_create_event(const char *name);
extern const char *afb_evt_event_name(struct afb_event event);

extern int afb_evt_add_watch(struct afb_evt_listener *listener, struct afb_event event);
extern int afb_evt_remove_watch(struct afb_evt_listener *listener, struct afb_event event);

