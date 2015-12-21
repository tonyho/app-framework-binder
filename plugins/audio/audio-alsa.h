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

#include "local-def.h"

typedef struct dev_ctx dev_ctx_T;

struct dev_ctx {
  char *name;
  snd_pcm_t *dev;
  snd_pcm_hw_params_t *params;
  snd_mixer_elem_t *mixer_elm;
  long vol_max;
  long vol;
  pthread_t thr;
  unsigned char thr_should_run;
  unsigned char thr_finished;
};

STATIC void* _play_thread_fn (void *);
PUBLIC unsigned int _alsa_get_volume (unsigned int);
PUBLIC unsigned char _alsa_get_mute (unsigned int);

static struct dev_ctx **dev_ctx = NULL;

#endif /* AUDIO_ALSA_H */
