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

struct locale_root;
struct locale_search;

extern struct locale_root *locale_root_create(int dirfd, const char *pathname);
extern struct locale_root *locale_root_addref(struct locale_root *root);
extern void locale_root_unref(struct locale_root *root);
extern struct locale_search *locale_root_search(struct locale_root *root, const char *definition, int immediate);

extern int locale_search_open(struct locale_search *search, const char *filename, int mode);
extern char *locale_search_resolve(struct locale_search *search, const char *filename);
extern struct locale_search *locale_search_addref(struct locale_search *search);
extern void locale_search_unref(struct locale_search *search);

