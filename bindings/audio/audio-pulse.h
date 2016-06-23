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

#ifndef AUDIO_PULSE_H
#define AUDIO_PULSE_H

#include <sys/time.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include "audio-alsa.h"

typedef struct dev_ctx_pulse dev_ctx_pulse_T;
typedef struct alsa_info alsa_info_T;

struct dev_ctx_pulse {
  char *sink_name;
  char **card_name;
  pa_mainloop *pa_loop;
  pa_context *pa_context;
  pa_simple *pa;
  pa_cvolume volume;
  int mute;
  unsigned char refresh;
  pthread_t thr;
  unsigned char thr_should_run;
  unsigned char thr_finished;
};

struct alsa_info {
  char *device;
  char *synonyms;
};

unsigned char _pulse_init (const char *, audioCtxHandleT *);
void _pulse_free (audioCtxHandleT *);
void _pulse_play (audioCtxHandleT *);
void _pulse_stop (audioCtxHandleT *);
unsigned int _pulse_get_volume (audioCtxHandleT *, unsigned int);
void _pulse_set_volume (audioCtxHandleT *, unsigned int, unsigned int);
void _pulse_set_volume_all (audioCtxHandleT *, unsigned int);
unsigned char _pulse_get_mute (audioCtxHandleT *);
void _pulse_set_mute (audioCtxHandleT *, unsigned char);

void  _pulse_context_cb (pa_context *, void *);
void  _pulse_sink_list_cb (pa_context *, const pa_sink_info *, int, void *);
void  _pulse_sink_info_cb (pa_context *, const pa_sink_info *, int, void *);
void* _pulse_play_thread_fn (void *);
void  _pulse_refresh_sink (dev_ctx_pulse_T *);

#endif /* AUDIO_PULSE_H */
