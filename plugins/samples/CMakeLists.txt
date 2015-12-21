INCLUDE_DIRECTORIES(${include_dirs})

ADD_LIBRARY(helloWorld-api MODULE HelloWorld.c)
SET_TARGET_PROPERTIES(helloWorld-api PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(helloWorld-api ${link_libraries})
INSTALL(TARGETS helloWorld-api
        LIBRARY DESTINATION ${plugin_install_dir})

ADD_LIBRARY(samplePost-api MODULE SamplePost.c)
SET_TARGET_PROPERTIES(samplePost-api PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(samplePost-api ${link_libraries})
INSTALL(TARGETS samplePost-api
        LIBRARY DESTINATION ${plugin_install_dir})
