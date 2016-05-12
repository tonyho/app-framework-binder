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

struct afb_ws;

struct afb_ws_itf
{
	void (*on_close) (void *, uint16_t code, char *, size_t size); /* optional, if not set hangup is called */
	void (*on_text) (void *, char *, size_t size);
	void (*on_binary) (void *, char *, size_t size);
	void (*on_error) (void *, uint16_t code, const void *, size_t size); /* optional, if not set hangup is called */
	void (*on_hangup) (void *); /* optional, it is safe too call afb_ws_destroy within the callback */
};

extern struct afb_ws *afb_ws_create(int fd, const struct afb_ws_itf *itf, void *closure);
extern void afb_ws_destroy(struct afb_ws *ws);
extern void afb_ws_hangup(struct afb_ws *ws);
extern int afb_ws_close(struct afb_ws *ws, uint16_t code, const char *reason);
extern int afb_ws_error(struct afb_ws *ws, uint16_t code, const char *reason);
extern int afb_ws_text(struct afb_ws *ws, const char *text, size_t length);
extern int afb_ws_texts(struct afb_ws *ws, ...);
extern int afb_ws_binary(struct afb_ws *ws, const void *data, size_t length);

