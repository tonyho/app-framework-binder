#include "audio-api.h"
#include "audio-alsa.h"

PUBLIC unsigned char _alsa_init (const char *name, audioCtxHandleT *ctx) {

    snd_pcm_t *dev;
    snd_pcm_hw_params_t *params;
    int num;

    if (snd_pcm_open (&dev, name, SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return 0;

    snd_pcm_hw_params_malloc (&params);
    snd_pcm_hw_params_any (dev, params);
    snd_pcm_hw_params_set_access (dev, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format (dev, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near (dev, params, &ctx->rate, 0);
    snd_pcm_hw_params_set_channels (dev, params, ctx->channels);
    if (snd_pcm_hw_params (dev, params) < 0) {
        snd_pcm_hw_params_free (params);
        return 0;
    }
    snd_pcm_prepare (dev);
    
    /* allocate the global array if it hasn't been done */
    if (!dev_ctx) {
        dev_ctx = (dev_ctx_T**) malloc (sizeof(dev_ctx_T));
        dev_ctx[0] = (dev_ctx_T*) malloc (sizeof(dev_ctx_T));
        dev_ctx[0]->name = NULL;
        dev_ctx[0]->dev = NULL;
    }

    /* is a card with similar name already opened ? */
    for (num = 0; num < (sizeof(dev_ctx)/sizeof(dev_ctx_T)); num++) {
        if (dev_ctx[num]->name &&
           !strcmp (dev_ctx[num]->name, name))
            return 0;
    }
    num++;

    /* it's not... let us add it to the global array */
    dev_ctx[num] = (dev_ctx_T*) malloc (sizeof(dev_ctx_T));
    dev_ctx[num]->name = strdup (name);
    dev_ctx[num]->dev = dev;
    dev_ctx[num]->params = params;

    return 1;
}

PUBLIC void _alsa_free (const char *name) {

    int num;

    for (num = 0; num < (sizeof(dev_ctx)/sizeof(dev_ctx_T)); num++) {
        if (dev_ctx[num]->name &&
           !strcmp (dev_ctx[num]->name, name)) {
            snd_pcm_close (dev_ctx[num]->dev);
            snd_pcm_hw_params_free (dev_ctx[num]->params);
            free (dev_ctx[num]->name);
            dev_ctx[num]->name = NULL;
            dev_ctx[num]->dev = NULL;
            free(dev_ctx[num]);
            return;
        }
    }
}

PUBLIC void _alsa_play (unsigned int num, void *buf, int len) {

    if (!dev_ctx || !dev_ctx[num])
        return;

    int16_t *cbuf = (int16_t *)buf;
    int frames = len / 2;
    int res;

    if ((res = snd_pcm_writei (dev_ctx[num]->dev, cbuf, frames)) != frames) {
        snd_pcm_recover (dev_ctx[num]->dev, res, 0);
        snd_pcm_prepare (dev_ctx[num]->dev);
    }
    /* snd_pcm_drain (dev_ctx[num]->dev); */
}