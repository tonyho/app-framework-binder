/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
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

struct sd_event;
struct sd_bus;

extern struct sd_event *afb_common_get_event_loop();
extern struct sd_bus *afb_common_get_user_bus();
extern struct sd_bus *afb_common_get_system_bus();

extern void afb_common_default_locale_set(const char *locale);
extern const char *afb_common_default_locale_get();

extern int afb_common_rootdir_set(const char *rootdir);
extern int afb_common_rootdir_get_fd();
extern int afb_common_rootdir_open_locale(const char *filename, int flags, const char *locale);

