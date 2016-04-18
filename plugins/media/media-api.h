/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author "Manuel Bachmann"
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

#ifndef MEDIA_API_H
#define MEDIA_API_H

#include "media-rygel.h"

/* -------------- PLUGIN DEFINITIONS ----------------- */

/* private client context [will be destroyed when client leaves] */
typedef struct {
  void *media_server;          /* handle to implementation (Rygel...) */
  unsigned int index;          /* currently selected media file       */
} mediaCtxHandleT;

PUBLIC json_object* _rygel_list (mediaCtxHandleT *);

#endif /* MEDIA_API_H */
