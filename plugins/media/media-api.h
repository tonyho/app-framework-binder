/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author "Manuel Bachmann"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

PUBLIC char* _rygel_list (mediaCtxHandleT *);

#endif /* MEDIA_API_H */
