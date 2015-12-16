#ifndef AUDIO_API_H
#define AUDIO_API_H

#include "audio-alsa.h"

/* global plugin handle, should store everything we may need */
typedef struct {
  int devCount;
} pluginHandleT;
  
/* structure holding one audio card with current usage status */
typedef struct {
   char *name;
   void *handle;           /* handle to implementation (ALSA, PulseAudio...) */
 } audioDevT;

/* private client context [will be destroyed when client leaves] */
typedef struct {
  audioDevT *radio;        /* pointer to client audio card          */
  unsigned int volume;     /* audio volume : 0-100                  */
  unsigned int rate;       /* audio rate (Hz)                       */
  unsigned int channels;   /* audio channels : 1(mono)/2(stereo)... */
} audioCtxHandleT;


#endif /* AUDIO_API_H */