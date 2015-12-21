IF(alsa_FOUND)

  INCLUDE_DIRECTORIES(${include_dirs})

  ADD_LIBRARY(audio-api MODULE audio-api.c audio-alsa.c)
  SET_TARGET_PROPERTIES(audio-api PROPERTIES PREFIX "")
  INSTALL(TARGETS audio-api
          LIBRARY DESTINATION ${plugin_install_dir})

ENDIF(alsa_FOUND)
