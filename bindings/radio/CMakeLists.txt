INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(librtlsdr librtlsdr>=0.5.0)

IF(librtlsdr_FOUND)

  MESSAGE(STATUS "librtlsdr found ; will compile Radio binding... (binding)")
  INCLUDE_DIRECTORIES(${include_dirs} ${librtlsdr_INCLUDE_DIRS})
  ADD_LIBRARY(radio-api MODULE radio-api.c radio-rtlsdr.c)
  SET_TARGET_PROPERTIES(radio-api PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
  )
  TARGET_LINK_LIBRARIES(radio-api ${link_libraries} ${librtlsdr_LIBRARIES} -lm)
  INSTALL(TARGETS radio-api
          LIBRARY DESTINATION ${binding_install_dir})

ENDIF(librtlsdr_FOUND)
