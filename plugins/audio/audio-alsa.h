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

#ifndef AUDIO_ALSA_H
#define AUDIO_ALSA_H

#include <pthread.h>
#include <alsa/asoundlib.h>

#include "audio-api.h"

#define AUDIO_BUFFER "/tmp/audio_buf"

typedef struct dev_ctx_alsa dev_ctx_alsa_T;

struct dev_ctx_alsa {
  char *name;
  snd_pcm_t *dev;
  snd_pcm_hw_params_t *params;
  snd_mixer_elem_t *mixer_elm;
  snd_mixer_elem_t *mixer_elm_m;
  long vol_max;
  long vol;
  pthread_t thr;
  unsigned char thr_should_run;
  unsigned char thr_finished;
};

unsigned char _alsa_init (const char *, audioCtxHandleT *);
void _alsa_free (const char *);
void _alsa_play (int);
void _alsa_stop (int);
unsigned int _alsa_get_volume (int, unsigned int);
void _alsa_set_volume (int, unsigned int, unsigned int);
void _alsa_set_volume_all (int, unsigned int);
unsigned char _alsa_get_mute (int);
void _alsa_set_mute (int, unsigned char);
void _alsa_set_channels (int, unsigned int);

void* _alsa_play_thread_fn (void *);

#endif /* AUDIO_ALSA_H */
