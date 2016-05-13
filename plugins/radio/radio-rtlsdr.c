/*
 * Copyright (C) 2015, 2016 "IoT.bzh"
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "radio-api.h"
#include "radio-rtlsdr.h"

static unsigned int init_dev_count = 0;
static struct dev_ctx **dev_ctx = NULL;

/* ------------- RADIO RTLSDR IMPLEMENTATION ---------------- */

/* --- PUBLIC FUNCTIONS --- */

/* Radio initialization should be done only when user start the radio and not at plugin initialization
   Making this call too early would impose to restart the binder to detect a radio */
unsigned char _radio_on (unsigned int num, radioCtxHandleT *ctx) {
 
    if (num >= _radio_dev_count())
        return 0;
    
    if (init_dev_count < _radio_dev_count()) {
        init_dev_count = _radio_dev_count();
        dev_ctx = (dev_ctx_T**) realloc (dev_ctx, init_dev_count * sizeof(dev_ctx_T*));           
    }

    dev_ctx[num] = (dev_ctx_T*) malloc (sizeof(dev_ctx_T));
    dev_ctx[num]->dev = NULL;
    dev_ctx[num]->mode = ctx->mode;
    dev_ctx[num]->freq = ctx->freq;
    dev_ctx[num]->mute = ctx->mute;
    dev_ctx[num]->should_run = 0;
    dev_ctx[num]->dongle = NULL;
    dev_ctx[num]->demod = NULL;
    dev_ctx[num]->output = NULL;
    _radio_dev_init (dev_ctx[num], num);

    return 1;
}

void _radio_off (unsigned int num) {

    if (num >= _radio_dev_count())
        return;

    if (dev_ctx[num]) {
        _radio_dev_free (dev_ctx[num]);
        free (dev_ctx[num]);
    }
    
    /* free(dev_ctx); */
}

void _radio_set_mode (unsigned int num, Mode mode) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    dev_ctx[num]->mode = mode;
    _radio_apply_params (dev_ctx[num]);
}

void _radio_set_freq (unsigned int num, double freq) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    dev_ctx[num]->freq = (float)freq;
    _radio_apply_params (dev_ctx[num]);
}

void _radio_set_mute (unsigned int num, unsigned char mute) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    dev_ctx[num]->mute = mute;
    _radio_apply_params (dev_ctx[num]);
}

void _radio_play (unsigned int num) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    _radio_start_threads (dev_ctx[num]);
}

void _radio_stop (unsigned int num) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    _radio_stop_threads (dev_ctx[num]);
}

unsigned int _radio_dev_count () {
    return rtlsdr_get_device_count();
}

const char* _radio_dev_name (unsigned int num) {
    return rtlsdr_get_device_name (num);
}


/* --- LOCAL HELPER FUNCTIONS --- */

unsigned char _radio_dev_init (dev_ctx_T *dev_ctx, unsigned int num) {
    rtlsdr_dev_t *dev = dev_ctx->dev;

    if (rtlsdr_open (&dev, num) < 0)
        return 0;

    rtlsdr_set_tuner_gain_mode (dev, 0);

    if (rtlsdr_reset_buffer (dev) < 0)
        return 0;

    dev_ctx->dev = dev;

    _radio_apply_params (dev_ctx);

    return 1;
}

unsigned char _radio_dev_free (dev_ctx_T *dev_ctx) {
    rtlsdr_dev_t *dev = dev_ctx->dev;

    if (rtlsdr_close (dev) < 0)
        return 0;
    dev = NULL;

    dev_ctx->dev = dev;

    return 1;
}

void _radio_apply_params (dev_ctx_T *dev_ctx) {
    rtlsdr_dev_t *dev = dev_ctx->dev;
    Mode mode = dev_ctx->mode;
    float freq = dev_ctx->freq;
    int rate;

    freq *= 1000000;
    rate = ((1000000 / 200000) + 1) * 200000;

    if (mode == FM)
        freq += 16000;
    freq += rate / 4;

    rtlsdr_set_center_freq (dev, freq);
    rtlsdr_set_sample_rate (dev, rate);

    dev_ctx->dev = dev;
}

void _radio_start_threads (dev_ctx_T *dev_ctx) {
    dev_ctx->dongle = (dongle_ctx*) malloc (sizeof(dongle_ctx));
    dev_ctx->demod = (demod_ctx*) malloc (sizeof(demod_ctx));
    dev_ctx->output = (output_ctx*) malloc (sizeof(output_ctx));

    dongle_ctx *dongle = dev_ctx->dongle;
    demod_ctx *demod = dev_ctx->demod;
    output_ctx *output = dev_ctx->output;

    pthread_rwlock_init (&demod->lck, NULL);
    pthread_cond_init (&demod->ok, NULL);
    pthread_mutex_init (&demod->ok_m, NULL);
    pthread_rwlock_init (&output->lck, NULL);
    pthread_cond_init (&output->ok, NULL);
    pthread_mutex_init (&output->ok_m, NULL);

    dev_ctx->should_run = 1;

     /* dongle thread */
    dongle->thr_finished = 0;
    pthread_create (&dongle->thr, NULL, _dongle_thread_fn, (void*)dev_ctx);

     /* demod thread */
    demod->pre_r = demod->pre_j = 0;
    demod->now_r = demod->now_j = 0;
    demod->index = demod->pre_index = demod->now_index = 0;
    demod->thr_finished = 0;
    pthread_create (&demod->thr, NULL, _demod_thread_fn, (void*)dev_ctx);

     /* output thread */
    output->thr_finished = 0;
    pthread_create (&output->thr, NULL, _output_thread_fn, (void*)dev_ctx);
}

void _radio_stop_threads (dev_ctx_T *dev_ctx) {
    rtlsdr_dev_t *dev = dev_ctx->dev;
    dongle_ctx *dongle = dev_ctx->dongle;
    demod_ctx *demod = dev_ctx->demod;
    output_ctx *output = dev_ctx->output;

    if (!dongle || !demod || !output)
        return;

     /* stop each "while" loop in threads */
    dev_ctx->should_run = 0;

    rtlsdr_cancel_async (dev);
    pthread_signal (&demod->ok, &demod->ok_m);
    pthread_signal (&output->ok, &output->ok_m);

    while (!dongle->thr_finished ||
           !demod->thr_finished ||
           !output->thr_finished)
        usleep (100000);

    pthread_join (dongle->thr, NULL);
    pthread_join (demod->thr, NULL);
    pthread_join (output->thr, NULL);
    pthread_rwlock_destroy (&demod->lck);
    pthread_cond_destroy (&demod->ok);
    pthread_mutex_destroy (&demod->ok_m);
    pthread_rwlock_destroy (&output->lck);
    pthread_cond_destroy (&output->ok);
    pthread_mutex_destroy (&output->ok_m);

    free (dongle); dev_ctx->dongle = NULL;
    free (demod); dev_ctx->demod = NULL;
    free (output); dev_ctx->output = NULL;
}

 /* ---- LOCAL THREADED FUNCTIONS ---- */

static void _rtlsdr_callback (unsigned char *buf, uint32_t len, void *ctx) {
    dev_ctx_T *dev_ctx = (dev_ctx_T *)ctx;
    dongle_ctx *dongle = dev_ctx->dongle;
    demod_ctx *demod = dev_ctx->demod;
    unsigned char tmp;
    int i;

    if (!dev_ctx->should_run)
        return;

     /* rotate 90° */
    for (i = 0; i < (int)len; i += 8) {
        tmp = 255 - buf[i+3];
        buf[i+3] = buf[i+2];
        buf[i+2] = tmp;

        buf[i+4] = 255 - buf[i+4];
        buf[i+5] = 255 - buf[i+5];

        tmp = 255 - buf[i+6];
        buf[i+6] = buf[i+7];
        buf[i+7] = tmp;
    }

     /* write data */
    for (i = 0; i < (int)len; i++)
        dongle->buf[i] = (int16_t)buf[i] - 127;

     /* lock demod thread, write to it, unlock */
       pthread_rwlock_wrlock (&demod->lck);
    memcpy (demod->buf, dongle->buf, 2 * len);
    demod->buf_len = len;
       pthread_rwlock_unlock (&demod->lck);
       pthread_signal (&demod->ok, &demod->ok_m);
}
 /**/
static void* _dongle_thread_fn (void *ctx) {
    dev_ctx_T *dev_ctx = (dev_ctx_T *)ctx;
    dongle_ctx *dongle = dev_ctx->dongle;

    rtlsdr_read_async (dev_ctx->dev, _rtlsdr_callback, dev_ctx, 0, 0);

    dongle->thr_finished = 1;
    return 0;
}

static void _lowpass_demod (void *ctx) {
    demod_ctx *demod = (demod_ctx *)ctx;
    int i=0, i2=0;

    while (i < demod->buf_len) {
        demod->now_r += demod->buf[i];
        demod->now_j += demod->buf[i+1];
        i += 2;
        demod->index++;
        if (demod->index < ((1000000 / 200000) + 1))
            continue;
        demod->buf[i2] = demod->now_r;
        demod->buf[i2+1] = demod->now_j;
        demod->index = 0;
        demod->now_r = demod->now_j = 0;
        i2 += 2;
    }
    demod->buf_len = i2;
}
 /**/
static void _lowpassreal_demod (void *ctx) {
    demod_ctx *demod = (demod_ctx *)ctx;
    int i=0, i2=0;
    int fast = 200000;
    int slow = 48000;

    while (i < demod->res_len) {
        demod->now_index += demod->res[i];
        i++;
        demod->pre_index += slow;
        if (demod->pre_index < fast)
            continue;
        demod->res[i2] = (int16_t)(demod->now_index / (fast/slow));
        demod->pre_index -= fast;
        demod->now_index = 0;
        i2 += 1;
    }
    demod->res_len = i2;
}
 /**/
static void _multiply (int ar, int aj, int br, int bj, int *cr, int *cj) {
    *cr = ar*br - aj*bj;
    *cj = aj*br + ar*bj;
}
 /**/
static int _polar_discriminant (int ar, int aj, int br, int bj) {
    int cr, cj;
    double angle;
    _multiply (ar, aj, br, -bj, &cr, &cj);
    angle = atan2 ((double)cj, (double)cr);
    return (int)(angle / 3.14159 * (1<<14));
}
 /**/
static void _fm_demod (void *ctx) {
    demod_ctx *demod = (demod_ctx *)ctx;
    int16_t *buf = demod->buf;
    int buf_len = demod->buf_len;
    int pcm, i;

    pcm = _polar_discriminant (buf[0], buf[1], demod->pre_r, demod->pre_j);
    demod->res[0] = (int16_t)pcm;

    for (i = 2; i < (buf_len-1); i += 2) {
        pcm = _polar_discriminant (buf[i], buf[i+1], buf[i-2], buf[i-1]);
        demod->res[i/2] = (int16_t)pcm;
    }
    demod->pre_r = buf[buf_len - 2];
    demod->pre_j = buf[buf_len - 1];
    demod->res_len = buf_len/2;
}
 /**/
static void _am_demod (void *ctx) {
    demod_ctx *demod = (demod_ctx *)ctx;
    int16_t *buf = demod->buf;
    int buf_len = demod->buf_len;
    int pcm, i;

    for (i = 0; i < buf_len; i += 2) {
        pcm = buf[i] * buf[i];
        pcm += buf[i+1] * buf[i+1];
        demod->res[i/2] = (int16_t)sqrt(pcm);
    }
    demod->res_len = buf_len/2;
}
 /**/
static void* _demod_thread_fn (void *ctx) {
    dev_ctx_T *dev_ctx = (dev_ctx_T *)ctx;
    demod_ctx *demod = dev_ctx->demod;
    output_ctx *output = dev_ctx->output;

    while(dev_ctx->should_run) {
            pthread_wait (&demod->ok, &demod->ok_m);
            pthread_rwlock_wrlock (&demod->lck);
        _lowpass_demod (demod);
        if (dev_ctx->mode == FM)
            _fm_demod (demod);
        else
            _am_demod (demod);
        _lowpassreal_demod (demod);
           pthread_rwlock_unlock (&demod->lck);

         /* lock demod thread, write to it, unlock */
           pthread_rwlock_wrlock (&output->lck);
        memcpy (output->buf, demod->res, 2 * demod->res_len);
        output->buf_len = demod->res_len;
           pthread_rwlock_unlock (&output->lck);
           pthread_signal (&output->ok, &output->ok_m);
    }

    demod->thr_finished = 1;
    return 0;
}

static void* _output_thread_fn (void *ctx) {
    dev_ctx_T *dev_ctx = (dev_ctx_T *)ctx;
    output_ctx *output = dev_ctx->output;
    FILE *file;

    file = fopen (AUDIO_BUFFER, "wb");

    while (dev_ctx->should_run) {
           pthread_wait (&output->ok, &output->ok_m);
           pthread_rwlock_rdlock (&output->lck);
           if (!dev_ctx->mute && file) {
               fwrite (output->buf, 2, output->buf_len, file);
               fflush (file);
               fseek (file, 0, SEEK_SET);
           }
           pthread_rwlock_unlock (&output->lck);
    }
    if (file) fclose(file);
    unlink (AUDIO_BUFFER);

    output->thr_finished = 1;
    return 0;
}
