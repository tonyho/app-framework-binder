/*
 * Copyright 2016 IoT.bzh
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


extern int afb_apis_count();

extern void afb_apis_free_context(int apiidx, void *context);

extern const struct AFB_restapi *afb_apis_get(int apiidx, int verbidx);

extern int afb_apis_get_verbidx(int apiidx, const char *name);

extern int afb_apis_get_apiidx(const char *prefix, size_t length);

extern int afb_apis_add_plugin(const char *path);

extern int afb_apis_add_directory(const char *path);

extern int afb_apis_add_path(const char *path);

extern int afb_apis_add_pathset(const char *pathset);

struct afb_req;
extern int afb_apis_handle(struct afb_req req, const char *api, size_t lenapi, const char *verb, size_t lenverb);

