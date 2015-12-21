IF(librtlsdr_FOUND)

  INCLUDE_DIRECTORIES(${include_dirs})

  ADD_LIBRARY(radio-api MODULE radio-api.c radio-rtlsdr.c)
  SET_TARGET_PROPERTIES(radio-api PROPERTIES PREFIX "")
  INSTALL(TARGETS radio-api
          LIBRARY DESTINATION ${plugin_install_dir})

ENDIF(librtlsdr_FOUND)
