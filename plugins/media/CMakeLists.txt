INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(gupnp gupnp-1.0 gupnp-av-1.0 gssdp-1.0 gobject-2.0 gio-2.0)

IF(gupnp_FOUND)

  MESSAGE(STATUS "gupnp found ; will compile Media plugin... (PLUGIN)")

  INCLUDE_DIRECTORIES( ${include_dirs} ${gupnp_INCLUDE_DIRS})
  ADD_LIBRARY(media-api MODULE media-api.c media-rygel.c)
  SET_TARGET_PROPERTIES(media-api PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
  )
  TARGET_LINK_LIBRARIES(media-api ${link_libraries} ${gupnp_LIBRARIES})
  INSTALL(TARGETS media-api
          LIBRARY DESTINATION ${plugin_install_dir})

ENDIF(gupnp_FOUND)
