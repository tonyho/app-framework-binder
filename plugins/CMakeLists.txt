SET(AUDIO_PLUGIN audio/audio-api.c)
SET(SESSION_PLUGIN session/token-api.c)
SET(SAMPLE_PLUGINS samples/HelloWorld.c)
IF(librtlsdr_FOUND)
  SET(RADIO_PLUGIN radio/radio-rtlsdr.c radio/radio-api.c)
ENDIF(librtlsdr_FOUND)

SET(PLUGINS_SOURCES ${AUDIO_PLUGIN} ${SESSION_PLUGIN} ${RADIO_PLUGIN} ${SAMPLE_PLUGINS})

ADD_LIBRARY(plugins OBJECT ${PLUGINS_SOURCES})
INCLUDE_DIRECTORIES(${include_dirs})
