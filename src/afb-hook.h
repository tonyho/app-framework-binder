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

/* individual flags */
#define afb_hook_flag_req_json			1
#define afb_hook_flag_req_get			2
#define afb_hook_flag_req_success		4
#define afb_hook_flag_req_fail			8
#define afb_hook_flag_req_raw			16
#define afb_hook_flag_req_send			32
#define afb_hook_flag_req_context_get		64
#define afb_hook_flag_req_context_set		128
#define afb_hook_flag_req_addref		256
#define afb_hook_flag_req_unref			512
#define afb_hook_flag_req_session_close		1024
#define afb_hook_flag_req_session_set_LOA	2048
#define afb_hook_flag_req_subscribe		4096
#define afb_hook_flag_req_unsubscribe		8192
#define afb_hook_flag_req_subcall		16384
#define afb_hook_flag_req_subcall_result	32768

/* common flags */
#define afb_hook_flags_req_args		(afb_hook_flag_req_json|afb_hook_flag_req_get)
#define afb_hook_flags_req_result	(afb_hook_flag_req_success|afb_hook_flag_req_fail)
#define afb_hook_flags_req_session	(afb_hook_flag_req_session_close|afb_hook_flag_req_session_set_LOA)
#define afb_hook_flags_req_event	(afb_hook_flag_req_subscribe|afb_hook_flag_req_unsubscribe)
#define afb_hook_flags_req_subcall	(afb_hook_flag_req_subcall|afb_hook_flag_req_subcall_result)

/* extra flags */
#define afb_hook_flags_req_ref		(afb_hook_flag_req_addref|afb_hook_flag_req_unref)
#define afb_hook_flags_req_context	(afb_hook_flag_req_context_get|afb_hook_flag_req_context_set)

/* internal flags */
#define afb_hook_flags_req_internal	(afb_hook_flag_req_raw|afb_hook_flag_req_send)

/* predefined groups */
#define afb_hook_flags_req_common	(afb_hook_flags_req_args|afb_hook_flags_req_result|afb_hook_flags_req_session|afb_hook_flags_req_event|afb_hook_flags_req_subcall)
#define afb_hook_flags_req_extra	(afb_hook_flags_req_common|afb_hook_flags_req_ref|afb_hook_flags_req_context)
#define afb_hook_flags_req_all		(afb_hook_flags_req_extra|afb_hook_flags_req_internal)

struct req;
struct afb_context;
struct json_object;
struct afb_arg;
struct afb_event;
struct AFB_clientCtx;

struct afb_hook;
struct afb_hook_req;

struct afb_hook_req_itf {
	/* life cycle of the request (no flag, always called) */
	void (*hook_req_begin)(void * closure, const struct afb_hook_req *tr);
	void (*hook_req_end)(void * closure, const struct afb_hook_req *tr);

	/* hook of actions on the request, subject to flags */
	void (*hook_req_json)(void * closure, const struct afb_hook_req *tr, struct json_object *obj);
	void (*hook_req_get)(void * closure, const struct afb_hook_req *tr, const char *name, struct afb_arg arg);
	void (*hook_req_success)(void * closure, const struct afb_hook_req *tr, struct json_object *obj, const char *info);
	void (*hook_req_fail)(void * closure, const struct afb_hook_req *tr, const char *status, const char *info);
	void (*hook_req_raw)(void * closure, const struct afb_hook_req *tr, const char *buffer, size_t size);
	void (*hook_req_send)(void * closure, const struct afb_hook_req *tr, const char *buffer, size_t size);
	void (*hook_req_context_get)(void * closure, const struct afb_hook_req *tr, void *value);
	void (*hook_req_context_set)(void * closure, const struct afb_hook_req *tr, void *value, void (*free_value)(void*));
	void (*hook_req_addref)(void * closure, const struct afb_hook_req *tr);
	void (*hook_req_unref)(void * closure, const struct afb_hook_req *tr);
	void (*hook_req_session_close)(void * closure, const struct afb_hook_req *tr);
	void (*hook_req_session_set_LOA)(void * closure, const struct afb_hook_req *tr, unsigned level, int result);
	void (*hook_req_subscribe)(void * closure, const struct afb_hook_req *tr, struct afb_event event, int result);
	void (*hook_req_unsubscribe)(void * closure, const struct afb_hook_req *tr, struct afb_event event, int result);
	void (*hook_req_subcall)(void * closure, const struct afb_hook_req *tr, const char *api, const char *verb, struct json_object *args);
	void (*hook_req_subcall_result)(void * closure, const struct afb_hook_req *tr, int status, struct json_object *result);
};

extern struct afb_req afb_hook_req_call(struct afb_req req, struct afb_context *context, const char *api, size_t lenapi, const char *verb, size_t lenverb);

extern struct afb_hook *afb_hook_req_create(const char *api, const char *verb, struct AFB_clientCtx *session, unsigned flags, struct afb_hook_req_itf *itf, void *closure);
extern struct afb_hook *afb_hook_addref(struct afb_hook *spec);
extern void afb_hook_unref(struct afb_hook *spec);

