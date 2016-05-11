/*
 * Copyright (C) 2015 "IoT.bzh"
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

#ifndef RADIO_API_H
#define RADIO_API_H

/* -------------- PLUGIN DEFINITIONS ----------------- */

#define MAX_RADIO 10
typedef enum { FM, AM } Mode;

/* structure holding one radio device with current usage status */
typedef struct {
    int   idx;
    char *name;
    int  used;
} radioDevT;

/* global plugin handle, should store everything we may need */
typedef struct {
  radioDevT *radios[MAX_RADIO];  // pointer to existing radio
  unsigned int devCount;
} pluginHandleT;

/* private client context [will be destroyed when client leaves] */
typedef struct {
    radioDevT *radio;         /* pointer to client radio            */
    int idx;                  /* radio index within global array    */
    Mode mode;                /* radio mode: AM/FM                  */
    float freq;               /* radio frequency (Mhz)              */
    unsigned char mute;       /* radio muted: 0(false)/1(true)      */
    unsigned char is_playing; /* radio is playing: 0(false)/1(true) */
} radioCtxHandleT;

#endif /* RADIO_API_H */
