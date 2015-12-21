SET(SESSION_PLUGIN session/token-api.c)
SET(HELLOWORLD_PLUGINS samples/HelloWorld.c)
SET(SAMPLEPOST_PLUGINS samples/SamplePost.c)
IF(alsa_FOUND)
  SET(AUDIO_PLUGIN audio/audio-api.c audio/audio-alsa.c)
ENDIF(alsa_FOUND)
IF(librtlsdr_FOUND)
  SET(RADIO_PLUGIN radio/radio-api.c radio/radio-rtlsdr.c)
ENDIF(librtlsdr_FOUND)

SET(PLUGINS_SOURCES ${SESSION_PLUGIN} ${HELLOWORLD_PLUGINS} ${SAMPLEPOST_PLUGINS} ${AUDIO_PLUGIN} ${RADIO_PLUGIN})

ADD_LIBRARY(plugins OBJECT ${PLUGINS_SOURCES})
INCLUDE_DIRECTORIES(${include_dirs})
