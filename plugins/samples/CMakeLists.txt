
INCLUDE_DIRECTORIES(${include_dirs})

##################################################
# AuthLogin
##################################################
ADD_LIBRARY(authLogin MODULE AuthLogin.c)
SET_TARGET_PROPERTIES(authLogin PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(authLogin ${link_libraries})
INSTALL(TARGETS authLogin
        LIBRARY DESTINATION ${plugin_install_dir})

##################################################
# DemoContext
##################################################
ADD_LIBRARY(demoContext MODULE DemoContext.c)
SET_TARGET_PROPERTIES(demoContext PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(demoContext ${link_libraries})
INSTALL(TARGETS demoContext
        LIBRARY DESTINATION ${plugin_install_dir})

##################################################
# DemoPost
##################################################
ADD_LIBRARY(demoPost MODULE DemoPost.c)
SET_TARGET_PROPERTIES(demoPost PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(demoPost ${link_libraries})
INSTALL(TARGETS demoPost
        LIBRARY DESTINATION ${plugin_install_dir})

##################################################
# HelloWorld
##################################################
ADD_LIBRARY(helloWorld MODULE HelloWorld.c)
SET_TARGET_PROPERTIES(helloWorld PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(helloWorld ${link_libraries})
INSTALL(TARGETS helloWorld
        LIBRARY DESTINATION ${plugin_install_dir})

##################################################
# tic-tac-toe
##################################################
ADD_LIBRARY(tic-tac-toe MODULE tic-tac-toe.c)
SET_TARGET_PROPERTIES(tic-tac-toe PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(tic-tac-toe ${link_libraries})
INSTALL(TARGETS tic-tac-toe
        LIBRARY DESTINATION ${plugin_install_dir})

