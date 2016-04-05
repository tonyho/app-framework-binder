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

#ifndef AUDIO_ALSA_H
#define AUDIO_ALSA_H

#include <pthread.h>
#include <alsa/asoundlib.h>

#include "audio-api.h"
#include "local-def.h"

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

PUBLIC unsigned char _alsa_init (const char *, audioCtxHandleT *);
PUBLIC void _alsa_free (const char *);
PUBLIC void _alsa_play (int);
PUBLIC void _alsa_stop (int);
PUBLIC unsigned int _alsa_get_volume (int, unsigned int);
PUBLIC void _alsa_set_volume (int, unsigned int, unsigned int);
PUBLIC void _alsa_set_volume_all (int, unsigned int);
PUBLIC unsigned char _alsa_get_mute (int);
PUBLIC void _alsa_set_mute (int, unsigned char);
PUBLIC void _alsa_set_channels (int, unsigned int);
STATIC void* _alsa_play_thread_fn (void *);

static struct dev_ctx_alsa **dev_ctx_a = NULL;

#endif /* AUDIO_ALSA_H */
