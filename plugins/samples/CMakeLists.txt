
INCLUDE_DIRECTORIES(${include_dirs})

ADD_LIBRARY(helloWorld-api MODULE HelloWorld.c)
SET_TARGET_PROPERTIES(helloWorld-api PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(helloWorld-api ${link_libraries})
INSTALL(TARGETS helloWorld-api
        LIBRARY DESTINATION ${plugin_install_dir})

ADD_LIBRARY(samplePost-api MODULE SamplePost.c)
SET_TARGET_PROPERTIES(samplePost-api PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(samplePost-api ${link_libraries})
INSTALL(TARGETS samplePost-api
        LIBRARY DESTINATION ${plugin_install_dir})

ADD_LIBRARY(clientCtx-api MODULE ClientCtx.c)
SET_TARGET_PROPERTIES(clientCtx-api PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(clientCtx-api ${link_libraries})
INSTALL(TARGETS clientCtx-api
        LIBRARY DESTINATION ${plugin_install_dir})

ADD_LIBRARY(tic-tac-toe MODULE tic-tac-toe.c)
SET_TARGET_PROPERTIES(tic-tac-toe PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(tic-tac-toe ${link_libraries})
INSTALL(TARGETS tic-tac-toe
        LIBRARY DESTINATION ${plugin_install_dir})
