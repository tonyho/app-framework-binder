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

#ifndef AUDIO_API_H
#define AUDIO_API_H

/* global plugin handle, should store everything we may need */
typedef struct {
  int devCount;
} pluginHandleT;

/* private client context [will be destroyed when client leaves] */
typedef struct {
  void *audio_dev;          /* handle to implementation (ALSA, PulseAudio...) */
  char *name;               /* name of the audio card */
  int idx;                  /* audio card index within global array           */
  unsigned int volume[8];   /* audio volume (8 channels) : 0-100              */
  unsigned int channels;    /* audio channels : 1(mono)/2(stereo)...          */
  unsigned char mute;       /* audio muted : 0(false)/1(true)                 */
  unsigned char is_playing; /* audio is playing: 0(false)/1(true)             */
} audioCtxHandleT;


#endif /* AUDIO_API_H */
