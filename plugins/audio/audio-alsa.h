#ifndef AUDIO_ALSA_H
#define AUDIO_ALSA_H

#include <alsa/asoundlib.h>

#include "local-def.h"

typedef struct dev_ctx dev_ctx_T;

struct dev_ctx {
  char *name;
  snd_pcm_t *dev;
  snd_pcm_hw_params_t *params;
};

static struct dev_ctx **dev_ctx = NULL;

#endif /* AUDIO_ALSA_H */