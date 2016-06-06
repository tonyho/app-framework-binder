
INCLUDE_DIRECTORIES(${include_dirs})

##################################################
# DBus Binding
##################################################
ADD_LIBRARY(afb-dbus-binding MODULE afb-dbus-binding.c)
SET_TARGET_PROPERTIES(afb-dbus-binding PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
)
TARGET_LINK_LIBRARIES(afb-dbus-binding ${link_libraries})
INSTALL(TARGETS afb-dbus-binding
        LIBRARY DESTINATION ${plugin_install_dir})

