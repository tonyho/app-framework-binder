SET(PLUGINS_SOURCES audio/audio-api.c session/token-api.c)

IF(librtlsdr_FOUND)
  SET(PLUGINS_SOURCES ${PLUGINS_SOURCES} radio/radio-api.c)
ENDIF(librtlsdr_FOUND)

ADD_LIBRARY(plugins OBJECT ${PLUGINS_SOURCES})
INCLUDE_DIRECTORIES(${include_dirs})
