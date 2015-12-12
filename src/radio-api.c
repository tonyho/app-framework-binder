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

typedef struct  {
    pthread_t thr;
    unsigned char thr_finished;
    uint16_t buf[BUF_LEN];
    uint32_t buf_len;
} dongle_ctx;

typedef struct  {
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
} demod_ctx;

typedef struct  {
    pthread_t thr;
    unsigned char thr_finished;
    pthread_rwlock_t lck;
    pthread_cond_t ok;
    pthread_mutex_t ok_m;
    int16_t buf[BUF_LEN];
    int buf_len;
} output_ctx;

typedef struct  {
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
} dev_ctx;


void* _dongle_thread_fn (void *);
void* _demod_thread_fn (void *);
void* _output_thread_fn (void *);
unsigned int _radio_dev_count (void);
const char* _radio_dev_name (unsigned int);
unsigned char _radio_dev_init (struct dev_ctx *, unsigned int);
unsigned char _radio_dev_free (struct dev_ctx *);
void _radio_apply_params (struct dev_ctx *);
void _radio_start_threads (struct dev_ctx *);
void _radio_stop_threads (struct dev_ctx *);

static unsigned int init_dev_count;
static dev_ctx **dev_ctx;

/* ------------- RADIO IMPLEMENTATION ----------------- */


// Radio initialisation should be done only when user start the radio and not at plugin intialisation
// Making this call too early would impose to restart the binder to detect a radio.
STATIC initRadio() {
 
    init_dev_count = _radio_dev_count();
    int i;

    dev_ctx = (dev_ctx**) malloc(init_dev_count * sizeof(dev_ctx));

    for (i = 0; i < init_dev_count; i++) {
        dev_ctx[i] = (struct dev_ctx*) malloc(sizeof(struct dev_ctx));
        dev_ctx[i]->dev = NULL;
        dev_ctx[i]->mode = FM;
        dev_ctx[i]->freq = 100.0;
        dev_ctx[i]->mute = 0;
        dev_ctx[i]->should_run = 0;
        dev_ctx[i]->dongle = NULL;
        dev_ctx[i]->demod = NULL;
        dev_ctx[i]->output = NULL;
        _radio_dev_init(dev_ctx[i], i);
    }
}

STATIC void radio_off () {
    int i;

    for (i = 0; i < init_dev_count; i++) {
        _radio_dev_free(dev_ctx[i]);
        free(dev_ctx[i]);
    }
    free(dev_ctx);
}

STATIC void radio_set_mode (dev_ctx *dev_ctx, Mode mode) {
    dev_ctx->mode = mode;
    _radio_apply_params(dev_ctx);
}

STATIC void radio_set_freq (dev_ctx *dev_ctx, float freq) {
    dev_ctx->freq = freq;
    _radio_apply_params(dev_ctx);
}

STATIC void radio_set_mute (dev_ctx *dev_ctx, unsigned char mute) {
    dev_ctx->mute = mute;
    _radio_apply_params(dev_ctx);
}

STATIC void radio_play (dev_ctx *dev_ctx) {
    _radio_start_threads(dev_ctx);
}

STATIC void radio_stop (dev_ctx *dev_ctx) {
    _radio_stop_threads(dev_ctx);
}

 /* --- HELPER FUNCTIONS --- */

STATIC unsigned int _radio_dev_count () {
    return rtlsdr_get_device_count();
}

STATIC const char* _radio_dev_name (unsigned int num) {
    return rtlsdr_get_device_name(num);
}

STATIC unsigned char _radio_dev_init (dev_ctx *dev_ctx, unsigned int num) {
    rtlsdr_dev_t *dev = dev_ctx->dev;

    if (rtlsdr_open(&dev, num) < 0)
        return 0;

    rtlsdr_set_tuner_gain_mode(dev, 0);

    if (rtlsdr_reset_buffer(dev) < 0)
        return 0;

    // dev_ctx->dev = dev; REQUIRED IN C TOO ? TEST !

    _radio_apply_params(dev_ctx);

    return 1;
}

STATIC unsigned char _radio_dev_free (dev_ctx *dev_ctx) {
    rtlsdr_dev_t *dev = dev_ctx->dev;

    if (rtlsdr_close(dev) < 0)
        return 0;
    dev = NULL;

    return 1;
}

STATIC void _radio_apply_params (dev_ctx *dev_ctx) {
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

    // dev_ctx->dev = dev; REQUIRED IN C TOO ? TEST !
}

STATIC void _radio_start_threads (dev_ctx *dev_ctx) {
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

STATIC void _radio_stop_threads (dev_ctx *dev_ctx) {
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
    dev_ctx *dev_ctx = (dev_ctx *)ctx;
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
    dev_ctx *dev_ctx = (dev_ctx *)ctx;
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
    dev_ctx *dev_ctx = (dev_ctx *)ctx;
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
    dev_ctx *dev_ctx = (dev_ctx *)ctx;
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



STATIC json_object* start (AFB_session *session, AFB_request *request, void* handle) {
    json_object *response;
    char query [512];

    // request all query key/value
    getQueryAll (request, query, sizeof(query));

    // check if we have some post data
    if (request->post == NULL)  request->post="NoData";

    // return response to caller
    response = jsonNewMessage(AFB_SUCCESS, "Start Radio plugin query={%s} PostData: \'%s\' ", query, request->post);

    //if (verbose) fprintf(stderr, "%d: \n", pingcount);
    return (response);
}

STATIC json_object* stop (AFB_session *session, AFB_request *request, void* handle) {
    json_object *response;
    char query [512];

    getQueryAll (request, query, sizeof(query));

    if (request->post == NULL)  request->post="NoData";

    response = jsonNewMessage(AFB_SUCCESS, "Stop Radio plugin query={%s} PostData: \'%s\' ", query, request->post);

    return (response);
}


// ********************************************************

// FULUP integration proposal with client session context

// ********************************************************


#define MAX_RADIO 10

// Structure holding existing radio with current usage status
typdef struct {
    int   idx;
    char *name;
    int  used;
}radioDevT;

// Radio plugin handle should store everething API may need
typdef struc {
  radioT *radios[MAX_RADIO];  // pointer to existing radio
  int devCount;
    
} pluginHandleT;

// Client Context Structure Hold any specific to client [will be destroyed when client leave]
typdef struc {
    dev_ctx radio; // pointer to client radio
    int     idx;   // index of radio within global array
    
} clientHandleT;


// It his was not a demo only, it should be smarter to enable hot plug/unplug
STATIC updateRadioDevList(pluginHandleT *handle) {
  int idx;  
    
  // loop on existing radio if any
  for (idx = 0; idx < _radio_dev_count; idx++) {
      if (idx == MAX_RADIO) break;
      handle->radios[idx] = calloc(1, sizeof(radioDevT)) // use calloc to set used to FALSE
      handle->radios[idx]->name = _radio_dev_name(num); 
  }
  handle->devCount = _radio_dev_count;
}


// This is call at plugin load time [radio devices might still not be visible]
STATIC  pluginHandleT* initRadioPlugin() {
    
  // Allocate Plugin handle  
  pluginHandleT  *handle = calloc (1,sizeof (pluginHandleT)); // init handle with zero
  
  // Some initialisation steps
  updateRadioDevList(handle);
  
  return (handle);
}

// Stop a radio free related ressource and make it avaliable for other clients
STATIC AFB_error releaseRadio (pluginHandleT* handle, AFB_clientCtx *client) {
    
   // change radio status
   handle->radios[client->idx].used = FALSE;
   
   // stop related threads and free attached resources
   radio_stop (client->radio);
   
   // May be some further cleanup ????
   
   return (AFB_SUCCESS); // Could it fails ????
}


// Start a radio and reserve exclusive usage to requesting client
STATIC clientHandleT  *reserveRadio (pluginHandleT* handle) {
    clientHandleT *client;
    
   // loop on existing radio if any
    for (idx = 0; idx < _radio_dev_count; idx++) {
        if (handle->radios[client->idx].used = FALSE) break;
    }
    
    // No avaliable radio return now
    if (idx == MAX_RADIO) return (NULL);
    
   // Book radio
   handle->radios[client->idx].used = TRUE;
   
   // create client handle 
   client = calloc (1, sizeof (clientHandleT));
   
   // stop related threads and free attached resources
   radio_start (client->radio);
   
   // May be some things to do ????
   
   
   return (client);
}

// This is called when client session died [ex; client quit for more than 15mn]
STATIC freeRadio (clientHandleT *client) {
    
    releaseRadio (handle, client);
    free (client);
}


STATIC json_object* powerOnOff (AFB_session *session, AFB_request *request, void* handle) {
    json_object *jresp;
    dev_ctx *dev_ctx = (dev_ctx *)handle;
    AFB_clientCtx *client=request->client; // get client context from request
   
    // Make sure binder was started with client session
    if ((client != NULL) {
        request->errcode=MHD_HTTP_FORBIDDEN;
        return (jsonNewMessage(AFB_FAIL, "Radio binder need session [--token=xxxx]"));        
    }
     
    // If we have a handle radio was on let power it down
    if (client->handle != NULL) {
        
        releaseRadio (handle, client);  // poweroff client related radio
        
        jresp = json_object_new_object();
        json_object_object_add(jresp, "power", json_object_new_string ("off"));        
        return (jresp);
    }
        
    // request a new client context token and check result 
    if (AFB_UNAUTH == ctxTokenCreate (request)) {
        request->errcode=MHD_HTTP_UNAUTHORIZED;
        jresp= jsonNewMessage(AFB_FAIL, "You're not authorized to request a radio [make sure you have the right authentication token");
        return (jresp);
    }
    
    // Client is clean let's look it we have an avaliable radio to propose
    
    // make sure we have last hot plug dongle visible
    updateRadioDevList (handle); 
    
    // get try to get an unused radio
    client->handle = reserveRadio (handle);  
    if (client->handle == NULL) {
       return (jsonNewMessage(AFB_FAIL, "Sory No More Radio Avaliable")); 
    }  
    
    // At this point we should have something to retreive radio status before last poweroff [but this is only a demonstrator]
}


STATIC  AFB_restapi pluginApis[]= {
  {"power"  , (AFB_apiCB)powerOnOff , "Ping Application Framework"},
  {"start"  , (AFB_apiCB)start      , "Ping Application Framework"},
  {"stop"   , (AFB_apiCB)stop       , "Ping Application Framework"},
  {NULL}
};

PUBLIC AFB_plugin *radioRegister (AFB_session *session) {
    AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
    plugin->type  = AFB_PLUGIN_JSON;
    plugin->info  = "Application Framework Binder - Radio plugin";
    plugin->prefix  = "radio";
    plugin->apis  = pluginApis;
    
    plugin->handle = initRadioPlugin();
    plugin->freeHandleCB = freeRadio();

    return (plugin);
};
