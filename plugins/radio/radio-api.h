/*
 * Copyright (C) 2015 "IoT.bzh"
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

#ifndef RADIO_API_H
#define RADIO_API_H

#include "radio-rtlsdr.h"

/* -------------- PLUGIN DEFINITIONS ----------------- */

#define MAX_RADIO 10

/* structure holding one radio device with current usage status */
typedef struct {
    int   idx;
    char *name;
    int  used;
} radioDevT;

/* global plugin handle, should store everything we may need */
typedef struct {
  radioDevT *radios[MAX_RADIO];  // pointer to existing radio
  int devCount;
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
