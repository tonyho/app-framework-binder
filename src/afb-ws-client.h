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

struct afb_wsj1;
struct afb_wsj1_itf;

/*
 * Makes the WebSocket handshake at the 'uri' and if successful
 * instanciate a wsj1 websocket for this connection using 'itf' and 'closure'.
 * (see afb_wsj1_create).
 * Returns NULL in case of failure with errno set appriately.
 */
extern struct afb_wsj1 *afb_ws_client_connect_wsj1(const char *uri, struct afb_wsj1_itf *itf, void *closure);

struct sd_event;
extern struct sd_event *afb_ws_client_get_event_loop();

