IF(librtlsdr_FOUND)

  ADD_LIBRARY(radio-api MODULE radio-api.c radio-rtlsdr.c)
  SET_TARGET_PROPERTIES(radio-api PROPERTIES PREFIX "")
  TARGET_LINK_LIBRARIES(radio-api ${link_libraries})
  INCLUDE_DIRECTORIES(${include_dirs})
  INSTALL(TARGETS radio-api
          LIBRARY DESTINATION ${plugin_install_dir})

ENDIF(librtlsdr_FOUND)
