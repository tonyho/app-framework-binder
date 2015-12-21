IF(alsa_FOUND)

  ADD_LIBRARY(audio-api MODULE audio-api.c audio-alsa.c)
  SET_TARGET_PROPERTIES(audio-api PROPERTIES PREFIX "")
  TARGET_LINK_LIBRARIES(audio-api ${link_libraries})
  INCLUDE_DIRECTORIES(${include_dirs})
  INSTALL(TARGETS audio-api
          LIBRARY DESTINATION ${plugin_install_dir})

ENDIF(alsa_FOUND)
