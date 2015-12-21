

pkg_check_modules(EXTRAS REQUIRED dbus-1)
add_compile_options(${EXTRAS_CFLAGS})
include_directories(${EXTRAS_INCLUDE_DIRS})
link_libraries(${EXTRAS_LIBRARIES})

ADD_LIBRARY(afm-main-api MODULE afm-main-plugin.c utils-jbus.c)
SET_TARGET_PROPERTIES(afm-main-api PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(afm-main-api ${link_libraries})
INCLUDE_DIRECTORIES(${include_dirs})
INSTALL(TARGETS afm-main-api
          LIBRARY DESTINATION ${plugin_install_dir})

