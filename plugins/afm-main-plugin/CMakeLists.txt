
INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(libsystemd REQUIRED libsystemd>=222)

INCLUDE_DIRECTORIES(
	${INCLUDE_DIRS}
	${libsystemd_INCLUDE_DIRS}
)


ADD_LIBRARY(afm-main-api MODULE afm-main-plugin.c utils-jbus.c)
SET_TARGET_PROPERTIES(afm-main-api PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)

TARGET_LINK_LIBRARIES(afm-main-api ${link_libraries} ${libsystemd_LIBRARIES})
INCLUDE_DIRECTORIES(${include_dirs})
INSTALL(TARGETS afm-main-api
          LIBRARY DESTINATION ${plugin_install_dir})
