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


#include "local-def.h"

/* -------------- RADIO DEFINITIONS ------------------ */

#include <math.h>
#include <pthread.h>
#include <rtl-sdr.h>

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
    int used;  // radio is free ???
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

#define MAX_RADIO 10

// Structure holding existing radio with current usage status
typedef struct {
    int   idx;
    char *name;
    int  used;
} radioDevT;

// Radio plugin handle should store everething API may need
typedef struct {
  radioDevT *radios[MAX_RADIO];  // pointer to existing radio
  int devCount;
} pluginHandleT;

/* private client context [will be destroyed when client leaves] */
typedef struct {
    radioDevT *radio;       /* pointer to client radio            */
    int idx;                /* radio index within global array    */
    Mode mode;              /* radio mode: AM/FM                  */
    float freq;             /* radio frequency (Mhz)              */
    unsigned char mute;     /* radio muted: 0(false)/1(true)      */
} radioCtxHandleT;


STATIC void* _dongle_thread_fn (void *);
STATIC void* _demod_thread_fn (void *);
STATIC void* _output_thread_fn (void *);
STATIC unsigned int _radio_dev_count (void);
STATIC const char* _radio_dev_name (unsigned int);
STATIC unsigned char _radio_dev_init (struct dev_ctx *, unsigned int);
STATIC unsigned char _radio_dev_free (struct dev_ctx *);
STATIC void _radio_apply_params (struct dev_ctx *);
STATIC void _radio_start_threads (struct dev_ctx *);
STATIC void _radio_stop_threads (struct dev_ctx *);

static unsigned int init_dev_count = 0;
static struct dev_ctx **dev_ctx = NULL;

/* ------------- RADIO IMPLEMENTATION ----------------- */


// Radio initialization should be done only when user start the radio and not at plugin initialization
// Making this call too early would impose to restart the binder to detect a radio.
STATIC unsigned char _radio_on (unsigned int num, radioCtxHandleT *ctx) {
 
    if (num >= _radio_dev_count())
        return 0;
    
    if (init_dev_count < _radio_dev_count()) {
        init_dev_count = _radio_dev_count();
        dev_ctx = (dev_ctx_T**) realloc (dev_ctx, init_dev_count * sizeof(dev_ctx_T));           
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
    _radio_dev_init(dev_ctx[num], num);
    
    return 1;
}

STATIC void _radio_off (unsigned int num) {

    if (num >= _radio_dev_count())
        return;

    if (dev_ctx[num]) {
        _radio_dev_free(dev_ctx[num]);
        free(dev_ctx[num]);
    }
    /* free(dev_ctx); */
}

STATIC void _radio_set_mode (unsigned int num, Mode mode) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    dev_ctx[num]->mode = mode;
    _radio_apply_params(dev_ctx[num]);
}

STATIC void _radio_set_freq (unsigned int num, float freq) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    dev_ctx[num]->freq = freq;
    _radio_apply_params(dev_ctx[num]);
}

STATIC void _radio_set_mute (unsigned int num, unsigned char mute) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    dev_ctx[num]->mute = mute;
    _radio_apply_params(dev_ctx[num]);
}

STATIC void _radio_play (unsigned int num) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    _radio_start_threads(dev_ctx[num]);
}

STATIC void _radio_stop (unsigned int num) {
    if (!dev_ctx || !dev_ctx[num])
        return;

    _radio_stop_threads(dev_ctx[num]);
}

 /* --- HELPER FUNCTIONS --- */

STATIC unsigned int _radio_dev_count () {
    return rtlsdr_get_device_count();
}

STATIC const char* _radio_dev_name (unsigned int num) {
    return rtlsdr_get_device_name(num);
}

STATIC unsigned char _radio_dev_init (dev_ctx_T *dev_ctx, unsigned int num) {
    rtlsdr_dev_t *dev = dev_ctx->dev;

    if (rtlsdr_open(&dev, num) < 0)
        return 0;

    rtlsdr_set_tuner_gain_mode(dev, 0);

    if (rtlsdr_reset_buffer(dev) < 0)
        return 0;

    dev_ctx->dev = dev;

    _radio_apply_params(dev_ctx);

    return 1;
}

STATIC unsigned char _radio_dev_free (dev_ctx_T *dev_ctx) {
    rtlsdr_dev_t *dev = dev_ctx->dev;

    if (rtlsdr_close(dev) < 0)
        return 0;
    dev = NULL;

    dev_ctx->dev = dev;

    return 1;
}

STATIC void _radio_apply_params (dev_ctx_T *dev_ctx) {
    rtlsdr_dev_t *dev = dev_ctx->dev;
    Mode mode = dev_ctx->mode;
    float freq = dev_ctx->freq;
    int rate;

    freq *= 1000000;
    rate = ((1000000 / 200000) + 1) * 200000;

    if (mode == FM)
        freq += 16000;
    freq += rate / 4;

    rtlsdr_set_center_freq(dev, freq);
    rtlsdr_set_sample_rate(dev, rate);

    dev_ctx->dev = dev;
}

STATIC void _radio_start_threads (dev_ctx_T *dev_ctx) {
    rtlsdr_dev_t *dev = dev_ctx->dev;
    dev_ctx->dongle = (dongle_ctx*) malloc(sizeof(dongle_ctx));
    dev_ctx->demod = (demod_ctx*) malloc(sizeof(demod_ctx));
    dev_ctx->output = (output_ctx*) malloc(sizeof(output_ctx));

    dongle_ctx *dongle = dev_ctx->dongle;
    demod_ctx *demod = dev_ctx->demod;
    output_ctx *output = dev_ctx->output;

    pthread_rwlock_init(&demod->lck, NULL);
    pthread_cond_init(&demod->ok, NULL);
    pthread_mutex_init(&demod->ok_m, NULL);
    pthread_rwlock_init(&output->lck, NULL);
    pthread_cond_init(&output->ok, NULL);
    pthread_mutex_init(&output->ok_m, NULL);

    dev_ctx->should_run = 1;

     /* dongle thread */
    dongle->thr_finished = 0;
    pthread_create(&dongle->thr, NULL, _dongle_thread_fn, (void*)dev_ctx);

     /* demod thread */
    demod->pre_r = demod->pre_j = 0;
    demod->now_r = demod->now_j = 0;
    demod->index = demod->pre_index = demod->now_index = 0;
    demod->thr_finished = 0;
    pthread_create(&demod->thr, NULL, _demod_thread_fn, (void*)dev_ctx);

     /* output thread */
    output->thr_finished = 0;
    pthread_create(&output->thr, NULL, _output_thread_fn, (void*)dev_ctx);
}

STATIC void _radio_stop_threads (dev_ctx_T *dev_ctx) {
    rtlsdr_dev_t *dev = dev_ctx->dev;
    dongle_ctx *dongle = dev_ctx->dongle;
    demod_ctx *demod = dev_ctx->demod;
    output_ctx *output = dev_ctx->output;

    if (!dongle || !demod || !output)
        return;

     /* stop each "while" loop in threads */
    dev_ctx->should_run = 0;

    rtlsdr_cancel_async(dev);
    pthread_signal(&demod->ok, &demod->ok_m);
    pthread_signal(&output->ok, &output->ok_m);

    while (!dongle->thr_finished ||
           !demod->thr_finished ||
           !output->thr_finished)
        usleep(100000);

    pthread_join(dongle->thr, NULL);
    pthread_join(demod->thr, NULL);
    pthread_join(output->thr, NULL);
    pthread_rwlock_destroy(&demod->lck);
    pthread_cond_destroy(&demod->ok);
    pthread_mutex_destroy(&demod->ok_m);
    pthread_rwlock_destroy(&output->lck);
    pthread_cond_destroy(&output->ok);
    pthread_mutex_destroy(&output->ok_m);

    free(dongle); dev_ctx->dongle = NULL;
    free(demod); dev_ctx->demod = NULL;
    free(output); dev_ctx->output = NULL;
}

 /* ---- LOCAL THREADED FUNCTIONS ---- */

STATIC void _rtlsdr_callback (unsigned char *buf, uint32_t len, void *ctx) {
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
       pthread_rwlock_wrlock(&demod->lck);
    memcpy(demod->buf, dongle->buf, 2 * len);
    demod->buf_len = len;
       pthread_rwlock_unlock(&demod->lck);
       pthread_signal(&demod->ok, &demod->ok_m);
}
 /**/
STATIC void* _dongle_thread_fn (void *ctx) {
    dev_ctx_T *dev_ctx = (dev_ctx_T *)ctx;
    dongle_ctx *dongle = dev_ctx->dongle;

    rtlsdr_read_async(dev_ctx->dev, _rtlsdr_callback, dev_ctx, 0, 0);

    dongle->thr_finished = 1;
    return 0;
}

STATIC void _lowpass_demod (void *ctx) {
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
STATIC void _lowpassreal_demod (void *ctx) {
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
STATIC void _multiply (int ar, int aj, int br, int bj, int *cr, int *cj) {
    *cr = ar*br - aj*bj;
    *cj = aj*br + ar*bj;
}
 /**/
STATIC int _polar_discriminant (int ar, int aj, int br, int bj) {
    int cr, cj;
    double angle;
    _multiply(ar, aj, br, -bj, &cr, &cj);
    angle = atan2((double)cj, (double)cr);
    return (int)(angle / 3.14159 * (1<<14));
}
 /**/
STATIC void _fm_demod (void *ctx) {
    demod_ctx *demod = (demod_ctx *)ctx;
    int16_t *buf = demod->buf;
    int buf_len = demod->buf_len;
    int pcm, i;

    pcm = _polar_discriminant(buf[0], buf[1], demod->pre_r, demod->pre_j);
    demod->res[0] = (int16_t)pcm;

    for (i = 2; i < (buf_len-1); i += 2) {
        pcm = _polar_discriminant(buf[i], buf[i+1], buf[i-2], buf[i-1]);
        demod->res[i/2] = (int16_t)pcm;
    }
    demod->pre_r = buf[buf_len - 2];
    demod->pre_j = buf[buf_len - 1];
    demod->res_len = buf_len/2;
}
 /**/
STATIC void _am_demod (void *ctx) {
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
STATIC void* _demod_thread_fn (void *ctx) {
    dev_ctx_T *dev_ctx = (dev_ctx_T *)ctx;
    demod_ctx *demod = dev_ctx->demod;
    output_ctx *output = dev_ctx->output;

    while(dev_ctx->should_run) {
            pthread_wait(&demod->ok, &demod->ok_m);
            pthread_rwlock_wrlock(&demod->lck);
        _lowpass_demod(demod);
        if (dev_ctx->mode == FM)
            _fm_demod(demod);
        else
            _am_demod(demod);
        _lowpassreal_demod(demod);
           pthread_rwlock_unlock(&demod->lck);

         /* lock demod thread, write to it, unlock */
           pthread_rwlock_wrlock(&output->lck);
        memcpy(output->buf, demod->res, 2 * demod->res_len);
        output->buf_len = demod->res_len;
           pthread_rwlock_unlock(&output->lck);
           pthread_signal(&output->ok, &output->ok_m);
    }

    demod->thr_finished = 1;
    return 0;
}

STATIC void* _output_thread_fn (void *ctx) {
    dev_ctx_T *dev_ctx = (dev_ctx_T *)ctx;
    output_ctx *output = dev_ctx->output;

    while (dev_ctx->should_run) {
           pthread_wait(&output->ok, &output->ok_m);
           pthread_rwlock_rdlock(&output->lck);
        //if (!dev_ctx->mute)
        //    mRadio->PlayAlsa((void*)&output->buf, output->buf_len);
           pthread_rwlock_unlock(&output->lck);
    }

    output->thr_finished = 1;
    return 0;
}


/* ********************************************************

   FULUP integration proposal with client session context

   ******************************************************** */

// It his was not a demo only, it should be smarter to enable hot plug/unplug
STATIC void updateRadioDevList(pluginHandleT *handle) {
  int idx;  

  // loop on existing radio if any
  for (idx = 0; idx < _radio_dev_count(); idx++) {
      if (idx == MAX_RADIO) break;
      handle->radios[idx] = calloc(1, sizeof(radioDevT)); // use calloc to set used to FALSE
      handle->radios[idx]->name = (char *) _radio_dev_name(idx); 
  }
  handle->devCount = _radio_dev_count();
}


/* global plugin context creation ; at loading time [radio devices might still not be visible] */
STATIC pluginHandleT* initRadioPlugin() {

  pluginHandleT *handle;

  handle = calloc (1, sizeof(pluginHandleT));
  updateRadioDevList (handle);

  return handle;
}


/* private client context creation ; default values */
STATIC radioCtxHandleT* initRadioCtx () {

    radioCtxHandleT *ctx;

    ctx = malloc (sizeof(radioCtxHandleT));
    ctx->radio = NULL;
    ctx->idx = -1;
    ctx->mode = FM;
    ctx->freq = 100.0;
    ctx->mute = 0;

    return ctx;
}


/* reserve a radio device to requesting client, start it */
STATIC AFB_error reserveRadio (pluginHandleT *handle, radioCtxHandleT *ctx) {
    int idx;

    /* loop on all devices, find an unused one */
    for (idx = 0; idx < _radio_dev_count(); idx++) {
        if (idx == MAX_RADIO) break;
        if (handle->radios[idx]->used == FALSE) goto found_radio; /* found one */
    }
    return AFB_FAIL;

   found_radio:
    /* try to power it on, passing client context info such as frequency... */
    _radio_on (idx, ctx);
    /* TODO : try to re-iterate from the next ones if it failed ! */

    /* globally mark it as reserved */
    handle->radios[idx]->used = TRUE;

    /* store relevant info to client context (direct pointer, index) */
    ctx->radio = handle->radios[idx];
    ctx->idx = idx;

    return AFB_SUCCESS;
}

/* free a radio device from requesting client, stop it */
STATIC AFB_error releaseRadio (pluginHandleT *handle, radioCtxHandleT *ctx) {

   /* globally mark it as free */
   handle->radios[ctx->idx]->used = FALSE;

   /* power it off */
   _radio_off (ctx->idx);

   return AFB_SUCCESS;
}

// This is called when client session died [ex; client quit for more than 15mn]
STATIC json_object* freeRadio () {
    
    //releaseRadio (client->handle, client);
    //free (client);
}


STATIC json_object* power (AFB_request *request) {      /* AFB_SESSION_CREATE */
    
    pluginHandleT *handle = request->client->plugin->handle; 
    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;

    /* create a private client context if needed */
    if (!ctx) ctx = initRadioCtx();

    /* no "?value=" parameter : return current state */
    if (!value) {
        jresp = json_object_new_object();
        ctx->radio ?
            json_object_object_add (jresp, "power", json_object_new_string ("on"))
          : json_object_object_add (jresp, "power", json_object_new_string ("off"));
    }

    /* "?value=" parameter is "1" or "on" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "on") ) {
        if (!ctx->radio) {
            if (reserveRadio (handle, ctx) == AFB_FAIL) {
                request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
                return (jsonNewMessage (AFB_FAIL, "No more radio devices available"));
            }
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "power-on", json_object_new_string ("ok"));
    }

    /* "?value=" parameter is "0" or "off" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "off") ) {
        if (ctx->radio) {
            if (releaseRadio (handle, ctx) == AFB_FAIL) {
                request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
                return (jsonNewMessage (AFB_FAIL, "Unable to release radio device"));
            }
        }
        jresp = json_object_new_object();
        json_object_object_add (jresp, "power-off", json_object_new_string ("ok"));
    }

    return jresp;
}

STATIC json_object* mode (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    char *mode_str;

    /* no "?value=" parameter : return current state */
    if (!value) {
        jresp = json_object_new_object();
        ctx->mode ?
            json_object_object_add (jresp, "mode", json_object_new_string ("AM"))
          : json_object_object_add (jresp, "mode", json_object_new_string ("FM"));
    }

    /* "?value=" parameter is "1" or "on" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "AM") ) {
        mode_str = strdup ("mode-AM");
        ctx->mode = AM;
    }

    /* "?value=" parameter is "0" or "off" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "FM") ) {
        mode_str = strdup ("mode-FM");
        ctx->mode = FM;
    }

    else {
        request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
        return (jsonNewMessage (AFB_FAIL, "Invalid value for mode"));
    }
       
    _radio_set_mode (ctx->idx, ctx->mode);
        
    jresp = json_object_new_object();
    json_object_object_add (jresp, mode_str, json_object_new_string ("ok"));
    
    return jresp;
}

STATIC json_object* freq (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp = json_object_new_object();
    char *freq_str;

    /* no "?value=" parameter : return current state */
    if (!value) {
        asprintf (&freq_str, "%f", ctx->freq);
        json_object_object_add (jresp, "freq", json_object_new_string (freq_str));
    }

    /* "?value=" parameter, set frequency */
    else {
        ctx->freq = strtof(value, NULL);
        _radio_set_freq (ctx->idx, ctx->freq);
        
        asprintf (&freq_str, "freq-%f", ctx->freq);
        json_object_object_add (jresp, freq_str, json_object_new_string ("ok"));
    }
    
    return jresp;
}

STATIC json_object* mute (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    char *mute_str;

    /* no "?value=" parameter : return current state */
    if (!value) {
        asprintf (&mute_str, "%d", ctx->mute);
        jresp = json_object_new_object();
        json_object_object_add (jresp, "mute", json_object_new_string (mute_str));
    }

    /* "?value=" parameter is "1" or "on" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "on") )
        ctx->mute = 1;

    /* "?value=" parameter is "0" or "off" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "off") )
        ctx->mute = 0;
        
    else {
        request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
        return (jsonNewMessage (AFB_FAIL, "Invalid value for mute"));
    }
       
    _radio_set_mute (ctx->idx, ctx->mute);
        
    asprintf (&mute_str, "mute-%d", ctx->mute);
    jresp = json_object_new_object();
    json_object_object_add (jresp, mute_str, json_object_new_string ("ok"));
    
    return jresp;
}

STATIC json_object* play (AFB_request *request) {        /* AFB_SESSION_CHECK */

    radioCtxHandleT *ctx = (radioCtxHandleT*)request->client->ctx;
    const char *value = getQueryValue (request, "value");
    json_object *jresp;
    
    if (!ctx->radio) {
        request->errcode = MHD_HTTP_SERVICE_UNAVAILABLE;
        return (jsonNewMessage (AFB_FAIL, "Radio device not powered on"));
    }

    /* "?value=" parameter is "1" or "on" */
    else if ( atoi(value) == 1 || !strcasecmp(value, "on") ) {
        /* radio playback */
        _radio_play (ctx->idx);
        jresp = json_object_new_object();
        json_object_object_add (jresp, "play-on", json_object_new_string ("ok"));
    }

    /* "?value=" parameter is "0" or "off" */
    else if ( atoi(value) == 0 || !strcasecmp(value, "off") ) {
        /* radio stop */
        _radio_stop (ctx->idx);
        jresp = json_object_new_object();
        json_object_object_add (jresp, "play-on", json_object_new_string ("ok"));
    }

    return jresp;
}

STATIC json_object* status (AFB_request *request) {
    return NULL;
}


STATIC AFB_restapi pluginApis[]= {
  {"power"  , AFB_SESSION_CREATE, (AFB_apiCB)power      , "Radio API - power"},
  {"mode"   , AFB_SESSION_CHECK,  (AFB_apiCB)mode       , "Radio API - mode"},
  {"freq"   , AFB_SESSION_CHECK,  (AFB_apiCB)freq       , "Radio API - freq"},
  {"mute"   , AFB_SESSION_CHECK,  (AFB_apiCB)mute       , "Radio API - mute"},
  {"play"   , AFB_SESSION_CHECK,  (AFB_apiCB)play       , "Radio API - play"},
  {"status" , AFB_SESSION_RENEW,  (AFB_apiCB)status     , "Radio API - status"},
  {NULL}
};

PUBLIC AFB_plugin* radioRegister (AFB_session *session) {
    AFB_plugin *plugin = malloc (sizeof(AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON;
    plugin->info  = "Application Framework Binder - Radio plugin";
    plugin->prefix  = "radio";
    plugin->apis  = pluginApis;

    plugin->handle = initRadioPlugin();
    plugin->freeCtxCB = freeRadio;

    return (plugin);
};
