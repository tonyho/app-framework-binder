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

#ifndef AUDIO_PULSE_H
#define AUDIO_PULSE_H

#include <sys/time.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include "audio-alsa.h"
#include "local-def.h"

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

PUBLIC unsigned char _pulse_init (const char *, audioCtxHandleT *);
PUBLIC void _pulse_free (audioCtxHandleT *);
PUBLIC void _pulse_play (audioCtxHandleT *);
PUBLIC void _pulse_stop (audioCtxHandleT *);
PUBLIC unsigned int _pulse_get_volume (audioCtxHandleT *, unsigned int);
PUBLIC void _pulse_set_volume (audioCtxHandleT *, unsigned int, unsigned int);
PUBLIC void _pulse_set_volume_all (audioCtxHandleT *, unsigned int);
PUBLIC unsigned char _pulse_get_mute (audioCtxHandleT *);
PUBLIC void _pulse_set_mute (audioCtxHandleT *, unsigned char);

STATIC void  _pulse_context_cb (pa_context *, void *);
STATIC void  _pulse_sink_list_cb (pa_context *, const pa_sink_info *, int, void *);
STATIC void  _pulse_sink_info_cb (pa_context *, const pa_sink_info *, int, void *);
STATIC void* _pulse_play_thread_fn (void *);
PUBLIC void  _pulse_refresh_sink (dev_ctx_pulse_T *);

static struct alsa_info **alsa_info = NULL;
static struct dev_ctx_pulse **dev_ctx_p = NULL;
static unsigned int client_count = 0;

#endif /* AUDIO_PULSE_H */
