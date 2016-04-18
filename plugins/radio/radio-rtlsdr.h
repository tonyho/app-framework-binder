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

#ifndef RADIO_RTLSDR_H
#define RADIO_RTLSDR_H

/* -------------- RADIO RTLSDR DEFINITIONS ------------------ */

#include <math.h>
#include <pthread.h>
#include <rtl-sdr.h>

#include "radio-api.h"
#include "local-def.h"

#define pthread_signal(n, m) pthread_mutex_lock(m); pthread_cond_signal(n); pthread_mutex_unlock(m)
#define pthread_wait(n, m) pthread_mutex_lock(m); pthread_cond_wait(n, m); pthread_mutex_unlock(m)
#define BUF_LEN 16*16384

typedef struct dongle_ctx dongle_ctx;
typedef struct demod_ctx demod_ctx;
typedef struct output_ctx output_ctx;
typedef struct dev_ctx dev_ctx_T;

struct dongle_ctx {
    pthread_t thr;
    unsigned char thr_finished;
    uint16_t buf[BUF_LEN];
    uint32_t buf_len;
};

struct demod_ctx {
    pthread_t thr;
    unsigned char thr_finished;
    pthread_rwlock_t lck;
    pthread_cond_t ok;
    pthread_mutex_t ok_m;
    int pre_r, pre_j, now_r, now_j, index;
    int pre_index, now_index;
    int16_t buf[BUF_LEN];
    int buf_len;
    int16_t res[BUF_LEN];
    int res_len;
};

struct output_ctx {
    pthread_t thr;
    unsigned char thr_finished;
    pthread_rwlock_t lck;
    pthread_cond_t ok;
    pthread_mutex_t ok_m;
    int16_t buf[BUF_LEN];
    int buf_len;
};

struct dev_ctx {
    int used;  /* TODO: radio is free ??? */
    rtlsdr_dev_t* dev;
    Mode mode;
    float freq;
    unsigned char mute;
    unsigned char should_run;
     /* thread contexts */
    dongle_ctx *dongle;
    demod_ctx *demod;
    output_ctx *output;
};

PUBLIC unsigned int _radio_dev_count (void);
PUBLIC const char* _radio_dev_name (unsigned int);

PUBLIC unsigned char _radio_on (unsigned int, radioCtxHandleT *);
PUBLIC void _radio_off (unsigned int);
PUBLIC void _radio_stop (unsigned int);
PUBLIC void _radio_play (unsigned int);
PUBLIC void _radio_set_mode (unsigned int, Mode);
PUBLIC void _radio_set_freq (unsigned int, double);
PUBLIC void _radio_set_mute (unsigned int, unsigned char);

STATIC void* _dongle_thread_fn (void *);
STATIC void* _demod_thread_fn (void *);
STATIC void* _output_thread_fn (void *);
STATIC unsigned char _radio_dev_init (struct dev_ctx *, unsigned int);
STATIC unsigned char _radio_dev_free (struct dev_ctx *);
STATIC void _radio_apply_params (struct dev_ctx *);
STATIC void _radio_start_threads (struct dev_ctx *);
STATIC void _radio_stop_threads (struct dev_ctx *);

static unsigned int init_dev_count = 0;
static struct dev_ctx **dev_ctx = NULL;

#endif /* RADIO_RTLSDR_H */
