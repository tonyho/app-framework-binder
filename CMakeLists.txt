PROJECT(afb-daemon C)

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.8)
SET(CMAKE_BUILD_TYPE, Debug)
SET(CMAKE_POSITION_INDEPENDENT_CODE ON)

SET(PROJECT_NAME "AFB Daemon")
SET(PROJECT_PRETTY_NAME "Application Framework Binder Daemon")
SET(PROJECT_VERSION "0.4")

INCLUDE(FindPkgConfig)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(GNUInstallDirs)

###########################################################################

add_compile_options(-Wall -Wextra -Wconversion)
add_compile_options(-Wno-unused-parameter) # frankly not using a parameter does it care?
add_compile_options(-Wno-sign-compare -Wno-sign-conversion)
add_compile_options(-Werror=maybe-uninitialized)
add_compile_options(-Werror=implicit-function-declaration)
add_compile_options(-ffunction-sections -fdata-sections)
add_compile_options(-Wl,--gc-sections)
add_compile_options(-fPIC)
add_compile_options(-g)

set(CMAKE_C_FLAGS_PROFILING    "-g -O2 -pg -Wp,-U_FORTIFY_SOURCE")
set(CMAKE_C_FLAGS_DEBUG        "-g -O2 -ggdb -Wp,-U_FORTIFY_SOURCE")
set(CMAKE_C_FLAGS_RELEASE      "-g -O2")
set(CMAKE_C_FLAGS_CCOV         "-g -O2 --coverage")

###########################################################################

IF(CMAKE_BUILD_TYPE MATCHES Debug)
  CHECK_LIBRARY_EXISTS(efence malloc "" HAVE_LIBEFENCE)
  IF(HAVE_LIBEFENCE)
    MESSAGE(STATUS "Linking with ElectricFence for debugging purposes...")
    SET(libefence_LIBRARIES "-lefence")
  ENDIF(HAVE_LIBEFENCE)
ENDIF(CMAKE_BUILD_TYPE MATCHES Debug)

PKG_CHECK_MODULES(json-c REQUIRED json-c)

INCLUDE(FindThreads)
FIND_PACKAGE(Threads)

SET(include_dirs
	${INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}/include
	${json-c_INCLUDE_DIRS}
)

SET(link_libraries
	${libefence_LIBRARIES}
	${CMAKE_THREAD_LIBS_INIT}
	${json-c_LIBRARIES}
)

SET(plugin_install_dir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/afb)

ADD_DEFINITIONS(-DPLUGIN_INSTALL_DIR="${plugin_install_dir}")

ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(include)
ADD_SUBDIRECTORY(plugins)

