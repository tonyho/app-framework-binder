IF(gupnp_FOUND)

  ADD_LIBRARY(media-api MODULE media-api.c media-rygel.c)
  SET_TARGET_PROPERTIES(media-api PROPERTIES PREFIX "")
  TARGET_LINK_LIBRARIES(media-api ${link_libraries})
  INCLUDE_DIRECTORIES(${include_dirs})
  INSTALL(TARGETS media-api
          LIBRARY DESTINATION ${plugin_install_dir})

ENDIF(gupnp_FOUND)
