
ADD_LIBRARY(token-api MODULE token-api.c)
SET_TARGET_PROPERTIES(token-api PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(token-api ${link_libraries})
INCLUDE_DIRECTORIES(${include_dirs})
INSTALL(TARGETS token-api
        LIBRARY DESTINATION ${plugin_install_dir})
