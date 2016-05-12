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

#pragma once

struct afb_wsj1;
struct afb_wsj1_msg;

struct json_object;

struct afb_wsj1_itf {
	void (*on_hangup)(void *closure);
	void (*on_call)(void *closure, const char *api, const char *verb, struct afb_wsj1_msg *msg);
	void (*on_event)(void *closure, const char *event, struct afb_wsj1_msg *msg);
};

extern struct afb_wsj1 *afb_wsj1_create(int fd, struct afb_wsj1_itf *itf, void *closure);

extern void afb_wsj1_addref(struct afb_wsj1 *wsj1);
extern void afb_wsj1_unref(struct afb_wsj1 *wsj1);

extern int afb_wsj1_send_event_s(struct afb_wsj1 *wsj1, const char *event, const char *object);
extern int afb_wsj1_send_event_j(struct afb_wsj1 *wsj1, const char *event, struct json_object *object);

extern int afb_wsj1_call_s(struct afb_wsj1 *wsj1, const char *api, const char *verb, const char *object, void (*on_reply)(void *closure, struct afb_wsj1_msg *msg), void *closure);
extern int afb_wsj1_call_j(struct afb_wsj1 *wsj1, const char *api, const char *verb, struct json_object *object, void (*on_reply)(void *closure, struct afb_wsj1_msg *msg), void *closure);

extern int afb_wsj1_reply_ok_s(struct afb_wsj1_msg *msg, const char *object, const char *token);
extern int afb_wsj1_reply_ok_j(struct afb_wsj1_msg *msg, struct json_object *object, const char *token);

extern int afb_wsj1_reply_error_s(struct afb_wsj1_msg *msg, const char *object, const char *token);
extern int afb_wsj1_reply_error_j(struct afb_wsj1_msg *msg, struct json_object *object, const char *token);

extern void afb_wsj1_msg_addref(struct afb_wsj1_msg *msg);
extern void afb_wsj1_msg_unref(struct afb_wsj1_msg *msg);

extern int afb_wsj1_msg_is_call(struct afb_wsj1_msg *msg);
extern int afb_wsj1_msg_is_reply(struct afb_wsj1_msg *msg);
extern int afb_wsj1_msg_is_reply_ok(struct afb_wsj1_msg *msg);
extern int afb_wsj1_msg_is_reply_error(struct afb_wsj1_msg *msg);
extern int afb_wsj1_msg_is_event(struct afb_wsj1_msg *msg);

extern const char *afb_wsj1_msg_api(struct afb_wsj1_msg *msg);
extern const char *afb_wsj1_msg_verb(struct afb_wsj1_msg *msg);
extern const char *afb_wsj1_msg_event(struct afb_wsj1_msg *msg);
extern const char *afb_wsj1_msg_token(struct afb_wsj1_msg *msg);

extern const char *afb_wsj1_msg_object_s(struct afb_wsj1_msg *msg);
extern struct json_object *afb_wsj1_msg_object_j(struct afb_wsj1_msg *msg);

