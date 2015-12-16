IF(alsa_FOUND)
  ADD_DEFINITIONS(-DHAVE_AUDIO_PLUGIN=1)
ENDIF(alsa_FOUND)
IF(librtlsdr_FOUND)
  ADD_DEFINITIONS(-DHAVE_RADIO_PLUGIN=1)
ENDIF(librtlsdr_FOUND)

ADD_LIBRARY(src OBJECT main.c config.c session.c http-svc.c rest-api.c)
INCLUDE_DIRECTORIES(${include_dirs})
