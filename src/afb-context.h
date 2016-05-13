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

struct AFB_clientCtx;

struct afb_context
{
	struct AFB_clientCtx *session;
	union {
		unsigned flags;
		struct {
			unsigned created: 1;
			unsigned validated: 1;
			unsigned invalidated: 1;
			unsigned refreshing: 1;
			unsigned refreshed: 1;
			unsigned closing: 1;
			unsigned closed: 1;
		};
	};
	int api_index;
};

extern void afb_context_init(struct afb_context *context, struct AFB_clientCtx *session, const char *token);
extern int afb_context_connect(struct afb_context *context, const char *uuid, const char *token);
extern void afb_context_disconnect(struct afb_context *context);
extern const char *afb_context_sent_token(struct afb_context *context);
extern const char *afb_context_sent_uuid(struct afb_context *context);

extern void *afb_context_get(struct afb_context *context);
extern void afb_context_set(struct afb_context *context, void *value, void (*free_value)(void*));

extern void afb_context_close(struct afb_context *context);
extern void afb_context_refresh(struct afb_context *context);
extern int afb_context_check(struct afb_context *context);
extern int afb_context_create(struct afb_context *context);

