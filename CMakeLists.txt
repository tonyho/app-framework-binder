PROJECT(afb-daemon C)

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.8)
SET(CMAKE_BUILD_TYPE, Debug)
SET(CMAKE_POSITION_INDEPENDENT_CODE ON)

SET(PROJECT_NAME "AFB Daemon")
SET(PROJECT_PRETTY_NAME "Application Framework Binder Daemon")
SET(PROJECT_VERSION "0.1")

INCLUDE(FindPkgConfig)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(GNUInstallDirs)

CHECK_INCLUDE_FILES(magic.h HAVE_MAGIC_H)
CHECK_LIBRARY_EXISTS(magic magic_load "" HAVE_LIBMAGIC_SO)
IF(HAVE_MAGIC_H)
  IF(HAVE_LIBMAGIC_SO)
    SET(HAVE_LIBMAGIC "1")
  ENDIF(HAVE_LIBMAGIC_SO)
ENDIF(HAVE_MAGIC_H)
IF(NOT HAVE_LIBMAGIC)
  MESSAGE(FATAL_ERROR "\"magic.h\" or \"libmagic.so\" missing.
    Please install the \"file-devel\" or \"libmagic-dev\" package !")
ENDIF(NOT HAVE_LIBMAGIC)

IF(CMAKE_BUILD_TYPE MATCHES Debug)
  CHECK_LIBRARY_EXISTS(efence malloc "" HAVE_LIBEFENCE)
  IF(HAVE_LIBEFENCE)
    MESSAGE(STATUS "Linking with ElectricFence for debugging purposes...")
    SET(libefence_LIBRARIES "-lefence")
  ENDIF(HAVE_LIBEFENCE)
ENDIF(CMAKE_BUILD_TYPE MATCHES Debug)

INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(json-c REQUIRED json-c)
PKG_CHECK_MODULES(libmicrohttpd REQUIRED libmicrohttpd)
PKG_CHECK_MODULES(uuid REQUIRED uuid)
# Optional plugin dependencies
PKG_CHECK_MODULES(alsa alsa)
PKG_CHECK_MODULES(librtlsdr librtlsdr>=0.5.0)

IF(alsa_FOUND)
  MESSAGE(STATUS "ALSA found ; will compile Audio plugin... (PLUGIN)")
ENDIF(alsa_FOUND)
IF(librtlsdr_FOUND)
  MESSAGE(STATUS "librtlsdr found ; will compile Radio plugin... (PLUGIN)")
ENDIF(librtlsdr_FOUND)

INCLUDE(FindThreads)
FIND_PACKAGE(Threads)

SET(include_dirs ${INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include ${json-c_INCLUDE_DIRS} ${libmicrohttpd_INCLUDE_DIRS} ${uuid_INCLUDE_DIRS} ${alsa_INCLUDE_DIRS} ${librtlsdr_INCLUDE_DIRS})
SET(link_libraries ${json-c_LIBRARIES} ${libmicrohttpd_LIBRARIES} ${uuid_LIBRARIES} ${alsa_LIBRARIES} ${librtlsdr_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${libefence_LIBRARIES} -lmagic -lm -ldl)
SET(plugin_install_dir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/afb)

ADD_DEFINITIONS(-DPLUGIN_INSTALL_DIR="${plugin_install_dir}")

ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(plugins)

ADD_EXECUTABLE(afb-daemon $<TARGET_OBJECTS:src>)
INCLUDE_DIRECTORIES(${include_dirs})
TARGET_LINK_LIBRARIES(afb-daemon ${link_libraries})

INSTALL(TARGETS afb-daemon
        RUNTIME DESTINATION bin)
