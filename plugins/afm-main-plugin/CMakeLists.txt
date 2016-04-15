
ADD_LIBRARY(afm-main-api MODULE afm-main-plugin.c utils-sbus.c utils-jbus.c)
SET_TARGET_PROPERTIES(afm-main-api PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(afm-main-api ${link_libraries})
INCLUDE_DIRECTORIES(${include_dirs})
INSTALL(TARGETS afm-main-api
          LIBRARY DESTINATION ${plugin_install_dir})
