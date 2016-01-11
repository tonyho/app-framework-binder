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

#ifndef RADIO_RTLSDR_H
#define RADIO_RTLSDR_H

/* -------------- RADIO RTLSDR DEFINITIONS ------------------ */

#include <math.h>
#include <pthread.h>
#include <rtl-sdr.h>

#include "local-def.h"

#define pthread_signal(n, m) pthread_mutex_lock(m); pthread_cond_signal(n); pthread_mutex_unlock(m)
#define pthread_wait(n, m) pthread_mutex_lock(m); pthread_cond_wait(n, m); pthread_mutex_unlock(m)
#define BUF_LEN 16*16384

typedef enum { FM, AM } Mode;
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
