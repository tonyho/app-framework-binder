/*
 Copyright (C) 2016 "IoT.bzh"

 author: José Bollo <jose.bollo@iot.bzh>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#pragma once

struct afb_hsrv;
struct afb_hreq;
struct locale_root;

extern struct afb_hsrv *afb_hsrv_create();
extern void afb_hsrv_put(struct afb_hsrv *hsrv);

extern void afb_hsrv_stop(struct afb_hsrv *hsrv);
extern int afb_hsrv_start(struct afb_hsrv *hsrv, uint16_t port, unsigned int connection_timeout);
extern int afb_hsrv_set_cache_timeout(struct afb_hsrv *hsrv, int duration);
extern int afb_hsrv_add_alias(struct afb_hsrv *hsrv, const char *prefix, const char *alias, int priority, int relax);
extern int afb_hsrv_add_alias_root(struct afb_hsrv *hsrv, const char *prefix, struct locale_root *root, int priority, int relax);
extern int afb_hsrv_add_handler(struct afb_hsrv *hsrv, const char *prefix, int (*handler) (struct afb_hreq *, void *), void *data, int priority);

