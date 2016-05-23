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

#define _GNU_SOURCE
#include <stdio.h>

#include "audio-api.h"
#include "audio-pulse.h"

static struct alsa_info **alsa_info = NULL;
static struct dev_ctx_pulse **dev_ctx_p = NULL;
static unsigned int client_count = 0;


unsigned char _pulse_init (const char *name, audioCtxHandleT *ctx) {

    pa_mainloop *pa_loop;
    pa_mainloop_api *pa_api;
    pa_context *pa_context;
    pa_simple *pa;
    pa_sample_spec *pa_spec;
    struct timeval tv_start, tv_now;
    int ret, error, i;

    pa_loop = pa_mainloop_new ();
    pa_api = pa_mainloop_get_api (pa_loop);
    pa_context = pa_context_new (pa_api, "afb-audio-plugin");

    /* allocate the global array if it hasn't been done */
    if (!dev_ctx_p)
        dev_ctx_p = (dev_ctx_pulse_T**) malloc (sizeof(dev_ctx_pulse_T*));

    /* create a temporary device, to be held until sink gets discovered */
    dev_ctx_pulse_T *dev_ctx_p_t = (dev_ctx_pulse_T*) malloc (sizeof(dev_ctx_pulse_T));
    dev_ctx_p_t->sink_name = NULL;
    dev_ctx_p_t->card_name = (char**) malloc (sizeof(char*));
    dev_ctx_p_t->card_name[0] = strdup (name);
    dev_ctx_p_t->pa_loop = pa_loop;
    dev_ctx_p_t->pa_context = pa_context;

    pa_context_set_state_callback (pa_context, _pulse_context_cb, (void*)dev_ctx_p_t);
    pa_context_connect (pa_context, NULL, 0, NULL);

    /* 1 second should be sufficient to retrieve sink info */
    gettimeofday (&tv_start, NULL);
    gettimeofday (&tv_now, NULL);
    while (tv_now.tv_sec - tv_start.tv_sec <= 1) {
        pa_mainloop_iterate (pa_loop, 0, &ret);

        if (ret == -1) {
			fprintf (stderr, "Stopping PulseAudio backend...\n");
            return 0;
        }
        if (ret >= 0) {
            /* found a matching sink from callback */
            fprintf (stderr, "Success : using sink n.%d\n", error);
            ctx->audio_dev = (void*)dev_ctx_p[ret];
            break;
        }
        gettimeofday (&tv_now, NULL);
    }
    /* fail if we found no matching sink */
    if (!ctx->audio_dev)
      return 0;

    /* make the client context aware of current card state */
    ctx->mute = (unsigned char)dev_ctx_p[ret]->mute;
    ctx->channels = (unsigned int)dev_ctx_p[ret]->volume.channels;
    for (i = 0; i < ctx->channels; i++)
        ctx->volume[i] = dev_ctx_p[ret]->volume.values[i];
    ctx->idx = ret;

    /* open matching sink for playback */
    pa_spec = (pa_sample_spec*) malloc (sizeof(pa_sample_spec));
    pa_spec->format = PA_SAMPLE_S16LE;
    pa_spec->rate = 22050;
    pa_spec->channels = (uint8_t)ctx->channels;

    if (!(pa = pa_simple_new (NULL, "afb-audio-plugin", PA_STREAM_PLAYBACK, dev_ctx_p[ret]->sink_name,
                              "afb-audio-output", pa_spec, NULL, NULL, &error))) {
        fprintf (stderr, "Error opening PulseAudio sink %s : %s\n",
                          dev_ctx_p[ret]->sink_name, pa_strerror(error));
        return 0;
    }
    dev_ctx_p[ret]->pa = pa;
    free (pa_spec);

    client_count++;

    fprintf (stderr, "Successfully initialized PulseAudio backend.\n");

    return 1;
}

void _pulse_free (audioCtxHandleT *ctx) {

    int num, i;

    client_count--;
    if (client_count > 0) return;

    for (num = 0; num < (sizeof(dev_ctx_p)/sizeof(dev_ctx_pulse_T)); num++) {

         for (i = 0; num < (sizeof(dev_ctx_p[num]->card_name)/sizeof(char*)); i++) {
             free (dev_ctx_p[num]->card_name[i]);
             dev_ctx_p[num]->card_name[i] = NULL;
         }
         pa_context_disconnect (dev_ctx_p[num]->pa_context);
         pa_context_unref (dev_ctx_p[num]->pa_context);
         pa_mainloop_free (dev_ctx_p[num]->pa_loop);
         pa_simple_free (dev_ctx_p[num]->pa);
         free (dev_ctx_p[num]->sink_name);
         dev_ctx_p[num]->pa_context = NULL;
         dev_ctx_p[num]->pa_loop = NULL;
         dev_ctx_p[num]->pa = NULL;
         dev_ctx_p[num]->sink_name = NULL;
         free (dev_ctx_p[num]);
    }
}

void _pulse_play (audioCtxHandleT *ctx) {

    dev_ctx_pulse_T* dev_ctx_p_c = (dev_ctx_pulse_T*)ctx->audio_dev;

    if (!dev_ctx_p_c || dev_ctx_p_c->thr_should_run || access (AUDIO_BUFFER, F_OK) == -1)
        return;

    dev_ctx_p_c->thr_should_run = 1;
    dev_ctx_p_c->thr_finished = 0;
    pthread_create (&dev_ctx_p_c->thr, NULL, _pulse_play_thread_fn, (void*)dev_ctx_p_c);
}

void _pulse_stop (audioCtxHandleT *ctx) {

    dev_ctx_pulse_T* dev_ctx_p_c = (dev_ctx_pulse_T*)ctx->audio_dev;

    if (!dev_ctx_p_c || !dev_ctx_p_c->thr_should_run)
        return;

    dev_ctx_p_c->thr_should_run = 0;
    while (!dev_ctx_p_c->thr_finished)
        usleep(100000);
    pthread_join (dev_ctx_p_c->thr, NULL);
}

unsigned int _pulse_get_volume (audioCtxHandleT *ctx, unsigned int channel) {

    dev_ctx_pulse_T* dev_ctx_p_c = (dev_ctx_pulse_T*)ctx->audio_dev;

    if (!dev_ctx_p_c)
        return 0;

    _pulse_refresh_sink (dev_ctx_p_c);

    return dev_ctx_p_c->volume.values[channel];
}

void _pulse_set_volume (audioCtxHandleT *ctx, unsigned int channel, unsigned int vol) {

    dev_ctx_pulse_T* dev_ctx_p_c = (dev_ctx_pulse_T*)ctx->audio_dev;
    struct pa_cvolume volume;

    if (!dev_ctx_p_c)
        return;

    volume = dev_ctx_p_c->volume;
    volume.values[channel] = vol;

    pa_context_set_sink_volume_by_name (dev_ctx_p_c->pa_context, dev_ctx_p_c->sink_name,
                                        &volume, NULL, NULL);
    _pulse_refresh_sink (dev_ctx_p_c);
}

void _pulse_set_volume_all (audioCtxHandleT *ctx, unsigned int vol) {

    dev_ctx_pulse_T* dev_ctx_p_c = (dev_ctx_pulse_T*)ctx->audio_dev;
    struct pa_cvolume volume;

    if (!dev_ctx_p_c)
        return;

    pa_cvolume_init (&volume);
    pa_cvolume_set (&volume, dev_ctx_p_c->volume.channels, vol);

    pa_context_set_sink_volume_by_name (dev_ctx_p_c->pa_context, dev_ctx_p_c->sink_name,
                                        &volume, NULL, NULL);
    _pulse_refresh_sink (dev_ctx_p_c);
}

unsigned char _pulse_get_mute (audioCtxHandleT *ctx) {

    dev_ctx_pulse_T* dev_ctx_p_c = (dev_ctx_pulse_T*)ctx->audio_dev;

    if (!dev_ctx_p_c)
        return 0;

    _pulse_refresh_sink (dev_ctx_p_c);

    return (unsigned char)dev_ctx_p_c->mute;
}

void _pulse_set_mute (audioCtxHandleT *ctx, unsigned char mute) {

    dev_ctx_pulse_T* dev_ctx_p_c = (dev_ctx_pulse_T*)ctx->audio_dev;

    if (!dev_ctx_p_c)
        return;

    pa_context_set_sink_mute_by_name (dev_ctx_p_c->pa_context, dev_ctx_p_c->sink_name,
                                      (int)mute, NULL, NULL);
    _pulse_refresh_sink (dev_ctx_p_c);
}

 /* ---- LOCAL HELPER FUNCTIONS ---- */

void _pulse_refresh_sink (dev_ctx_pulse_T* dev_ctx_p_c) {

    dev_ctx_p_c->refresh = 1;

    pa_context_get_sink_info_by_name (dev_ctx_p_c->pa_context, dev_ctx_p_c->sink_name,
                                      _pulse_sink_info_cb, (void*)dev_ctx_p_c);

    while (dev_ctx_p_c->refresh)
        pa_mainloop_iterate (dev_ctx_p_c->pa_loop, 0, NULL);
}

void _pulse_enumerate_cards () {

    void **cards, **card;
    char *name, *found, *alsa_name, *card_name;
    int new_info, i, num = 0;

    /* allocate the global alsa array */
    alsa_info = (alsa_info_T**) malloc (sizeof(alsa_info_T));
    alsa_info[0] = (alsa_info_T*) malloc (sizeof(alsa_info_T));
    alsa_info[0]->device = NULL;
    alsa_info[0]->synonyms = NULL;

    /* we use ALSA to enumerate cards */
    snd_device_name_hint (-1, "pcm", &cards);
    card = cards;

    for (; *card != NULL; card++) {
        name = snd_device_name_get_hint (*card, "NAME");
        new_info = 1;

        /* alsa name is before ':' (if ':' misses, then it has no card) */
        found = strstr (name, ":");
        if (!found) continue;
        /* */
        alsa_name = (char*) malloc (found-name+1);
        strncpy (alsa_name, name, found-name);
        alsa_name[found-name] = '\0';

        /* card name is the invariant between "CARD=" and ',' */
        found = strstr (name, "CARD=");
        if (!found) continue;
        /* */
        found += 5;
        card_name = strdup (found);
        found = strstr (card_name, ",");
        if (found) card_name[found-card_name] = '\0';

        /* was the card name already listed in the global alsa array ? */
        for (i = 0; i < (sizeof(alsa_info)/sizeof(alsa_info_T)); i++) {

            if (alsa_info[i]->device &&
                !strcmp (alsa_info[i]->device, card_name)) {
                /* it was ; add the alsa name as a new synonym */
                asprintf (&alsa_info[i]->synonyms, "%s:%s", alsa_info[i]->synonyms, alsa_name);
                new_info = 0;
                break;
            }            
        }
        /* it was not ; create it */
        if (new_info) {
            alsa_info = (alsa_info_T**) realloc (alsa_info, (num+1)*sizeof(alsa_info_T));
            alsa_info[num]->device = strdup (card_name);
            asprintf (&alsa_info[num]->synonyms, ":%s", alsa_name);
            num++;
        }
        free (alsa_name);
        free (card_name);
    }
}

char** _pulse_find_cards (const char *name) {

    char **cards = NULL;
    char *needle, *found, *next;
    int num, i = 0;

    if (!alsa_info)
      _pulse_enumerate_cards ();

    asprintf (&needle, ":%s", name);

    for (num = 0; num < (sizeof(alsa_info)/sizeof(alsa_info_T)); num++) {

        found = strstr (alsa_info[num]->synonyms, needle);
        while (found) {
            /* if next character is not ':' or '\0', we are wrong */
            if ((found[strlen(name)] != ':') && (found[strlen(name)] != '\0')) {
                found = strstr (found+1, needle);
                continue;
            }
            /* found it ; now return all the "synonym" cards */
            found = strstr (alsa_info[num]->synonyms, ":");
            while (found) {
                next = strstr (found+1, ":");
                cards = (char**) realloc (cards, (i+1)*sizeof(char*));
                cards[i] = (char*) malloc (next-found+1);
                strncpy (cards[i], found+1, next-found);
                cards[i][next-found] = '\0';
                i++;
            }
        }
    }
    free (needle);

    return cards;
}

 /* ---- LOCAL CALLBACK FUNCTIONS ---- */

void _pulse_context_cb (pa_context *context, void *data) {

    pa_context_state_t state = pa_context_get_state (context);
    dev_ctx_pulse_T *dev_ctx_p_t = (dev_ctx_pulse_T *)data;

    if (state == PA_CONTEXT_FAILED) {
        fprintf (stderr, "Could not connect to PulseAudio !\n");
        pa_mainloop_quit (dev_ctx_p_t->pa_loop, -1);
    }

    if (state == PA_CONTEXT_READY)
        pa_context_get_sink_info_list (context, _pulse_sink_list_cb, (void*)dev_ctx_p_t);
}

void _pulse_sink_list_cb (pa_context *context, const pa_sink_info *info,
				 int eol, void *data) {

    dev_ctx_pulse_T *dev_ctx_p_t = (dev_ctx_pulse_T *)data;
    const char *device_string;
    char **cards;
    int num, i;

    if (eol != 0)
        return;

    /* ignore sinks with no cards */
    if (!pa_proplist_contains (info->proplist, "device.string"))
        return;

    device_string = pa_proplist_gets (info->proplist, "device.string");

    /* was a sink with similar name already found ? */
    for (num = 0; num < (sizeof(dev_ctx_p)/sizeof(dev_ctx_pulse_T)); num++) {
        if (dev_ctx_p[num]->sink_name &&
           !strcmp (dev_ctx_p[num]->sink_name, info->name)) {

            /* yet it was, did it have the required card ? */
            cards = dev_ctx_p[num]->card_name;
            for (i = 0; i < (sizeof(cards)/sizeof(char*)); i++) {
                if (!strcmp (cards[i], dev_ctx_p_t->card_name[0])) {
                    /* it did : stop there and succeed */
                    fprintf (stderr, "Found matching sink : %s\n", info->name);
                    pa_mainloop_quit (dev_ctx_p_t->pa_loop, num);
                }
            }
            /* it did not, ignore and return */
            return;
        }
    }
    num++;

    /* new sink, find all the cards it manages, fail if none */
    cards = _pulse_find_cards (device_string);
    if (!cards) return;

    /* everything is well, register it in global array */
    dev_ctx_p_t->sink_name = strdup (info->name);
    dev_ctx_p_t->card_name = cards;
    dev_ctx_p_t->mute = info->mute;
    dev_ctx_p_t->volume = info->volume;
    dev_ctx_p_t->thr_should_run = 0;
    dev_ctx_p_t->thr_finished = 0;
    dev_ctx_p[num] = dev_ctx_p_t;

    /* does this new sink have the card we are looking for ? */ /* TODO : factorize this */
    for (i = 0; i < (sizeof(cards)/sizeof(char*)); i++) {
        if (!strcmp (cards[i], dev_ctx_p_t->card_name[0])) {
             /* it did : stop there and succeed */
             fprintf (stderr, "Found matching sink : %s\n", info->name);
             pa_mainloop_quit (dev_ctx_p_t->pa_loop, num);
        }
    }
}

void _pulse_sink_info_cb (pa_context *context, const pa_sink_info *info,
				 int eol, void *data) {

    dev_ctx_pulse_T *dev_ctx_p_c = (dev_ctx_pulse_T *)data;

    if (eol != 0)
        return;

    dev_ctx_p_c->refresh = 0;
    dev_ctx_p_c->mute = info->mute;
    dev_ctx_p_c->volume = info->volume;
}

 /* ---- LOCAL THREADED FUNCTIONS ---- */

void* _pulse_play_thread_fn (void *ctx) {

    dev_ctx_pulse_T *dev_ctx_p_c = (dev_ctx_pulse_T *)ctx;
    FILE *file = NULL;
    char *buf = NULL;
    long size;
    int error;

    file = fopen (AUDIO_BUFFER, "rb");

    while (dev_ctx_p_c->thr_should_run && file && (access (AUDIO_BUFFER, F_OK) != -1) ) {
        fseek (file, 0, SEEK_END);
        size = ftell (file);
        buf = (char*) realloc (buf, size * sizeof(char));

        fseek (file, 0, SEEK_SET);
        fread (buf, 1, size, file);
        fflush (file);

        if (pa_simple_write (dev_ctx_p_c->pa, buf, size*2, &error) < 0)
            fprintf (stderr, "Error writing to PulseAudio : %s\n", pa_strerror (error));
        /* pa_simple_drain (dev_ctx_p_c->pa); */
    }
    if (buf) free(buf);
    if (file) fclose(file);

    dev_ctx_p_c->thr_finished = 1;
    return 0;
}
