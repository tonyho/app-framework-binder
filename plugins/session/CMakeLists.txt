INCLUDE_DIRECTORIES(${include_dirs})

ADD_LIBRARY(token-api MODULE token-api.c)
SET_TARGET_PROPERTIES(token-api PROPERTIES PREFIX "")
INSTALL(TARGETS token-api
        LIBRARY DESTINATION ${plugin_install_dir})
