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

struct json_object;

struct afb_context
{
	void *context;
	void (*free_context)(void*);
};

extern void *afb_context_get(struct afb_context *actx);
extern void afb_context_set(struct afb_context *actx, void *context, void (*free_context)(void*));

struct afb_event_sender_itf
{
	void (*send)(void *closure, const char *event, struct json_object *object);
};

struct afb_event_sender
{
	const struct afb_event_sender_itf *itf;
	void *closure;
};

struct afb_event_sender_list;

struct AFB_clientCtx
{
	time_t expiration;    // expiration time of the token
	int created;
	unsigned refcount;
	struct afb_context *contexts;
	char uuid[37];        // long term authentication of remote client
	char token[37];       // short term authentication of remote client
	struct afb_event_sender_list *senders;
};

extern void ctxStoreInit (int max_session_count, int timeout, const char *initok, int context_count);

extern struct AFB_clientCtx *ctxClientGetForUuid (const char *uuid);
extern struct AFB_clientCtx *ctxClientGet(struct AFB_clientCtx *clientCtx);
extern void ctxClientPut(struct AFB_clientCtx *clientCtx);
extern void ctxClientClose (struct AFB_clientCtx *clientCtx);

extern int ctxClientEventSenderAdd(struct AFB_clientCtx *clientCtx, struct afb_event_sender sender);
extern void ctxClientEventSenderRemove(struct AFB_clientCtx *clientCtx, struct afb_event_sender sender);
extern int ctxClientEventSend(struct AFB_clientCtx *clientCtx, const char *event, struct json_object *object);

extern int ctxTokenCheck (struct AFB_clientCtx *clientCtx, const char *token);
extern int ctxTokenCheckLen (struct AFB_clientCtx *clientCtx, const char *token, size_t length);
extern void ctxTokenNew (struct AFB_clientCtx *clientCtx);

