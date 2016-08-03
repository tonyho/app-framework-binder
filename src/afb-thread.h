/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author José Bollo <jose.bollo@iot.bzh>
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

struct afb_req;

extern void afb_thread_call(struct afb_req req, void (*callback)(struct afb_req req), int timeout, void *group);

extern int afb_thread_init(int allowed_count, int start_count, int waiter_count);
extern void afb_thread_terminate();

extern int afb_thread_timer_create();
extern int afb_thread_timer_arm(int timeout);
extern void afb_thread_timer_disarm();
extern void afb_thread_timer_delete();

