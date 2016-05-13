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
struct AFB_clientCtx;

struct afb_event_listener_itf
{
	void (*send)(void *closure, const char *event, struct json_object *object);
	int (*expects)(void *closure, const char *event);
};

struct afb_event_listener
{
	const struct afb_event_listener_itf *itf;
	void *closure;
};

extern void ctxStoreInit (int max_session_count, int timeout, const char *initok, int context_count);

extern struct AFB_clientCtx *ctxClientGetSession (const char *uuid, int *created);
extern struct AFB_clientCtx *ctxClientAddRef(struct AFB_clientCtx *clientCtx);
extern void ctxClientUnref(struct AFB_clientCtx *clientCtx);
extern void ctxClientClose (struct AFB_clientCtx *clientCtx);

extern int ctxClientEventListenerAdd(struct AFB_clientCtx *clientCtx, struct afb_event_listener listener);
extern void ctxClientEventListenerRemove(struct AFB_clientCtx *clientCtx, struct afb_event_listener listener);
extern int ctxClientEventSend(struct AFB_clientCtx *clientCtx, const char *event, struct json_object *object);

extern int ctxTokenCheck (struct AFB_clientCtx *clientCtx, const char *token);
extern void ctxTokenNew (struct AFB_clientCtx *clientCtx);

extern const char *ctxClientGetUuid (struct AFB_clientCtx *clientCtx);
extern const char *ctxClientGetToken (struct AFB_clientCtx *clientCtx);

extern void *ctxClientValueGet(struct AFB_clientCtx *clientCtx, int index);
extern void ctxClientValueSet(struct AFB_clientCtx *clientCtx, int index, void *value, void (*free_value)(void*));

