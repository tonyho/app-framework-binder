PROJECT(afb-daemon C)

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.8)
SET(CMAKE_BUILD_TYPE, Debug)
SET(CMAKE_POSITION_INDEPENDENT_CODE ON)

SET(PROJECT_NAME "AFB Daemon")
SET(PROJECT_PRETTY_NAME "Application Framework Binder Daemon")
SET(PROJECT_VERSION "0.3")

INCLUDE(FindPkgConfig)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(GNUInstallDirs)

###########################################################################

add_compile_options(-Wall -Wextra -Wconversion)
add_compile_options(-Wno-unused-parameter) # frankly not using a parameter does it care?
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
PKG_CHECK_MODULES(openssl REQUIRED openssl)
PKG_CHECK_MODULES(uuid REQUIRED uuid)
PKG_CHECK_MODULES(dbus REQUIRED dbus-1)
# Optional plugin dependencies
PKG_CHECK_MODULES(alsa alsa)
PKG_CHECK_MODULES(pulseaudio libpulse libpulse-simple)
PKG_CHECK_MODULES(librtlsdr librtlsdr>=0.5.0)
PKG_CHECK_MODULES(gupnp gupnp-1.0 gupnp-av-1.0 gssdp-1.0 gobject-2.0 gio-2.0)

IF(alsa_FOUND)
  MESSAGE(STATUS "ALSA found ; will compile Audio plugin... (PLUGIN)")
  IF(pulseaudio_FOUND)
    MESSAGE(STATUS "PulseAudio found ; Audio plugin will have PulseAudio support")
    ADD_DEFINITIONS(-DHAVE_PULSE=1)
  ENDIF(pulseaudio_FOUND)
ENDIF(alsa_FOUND)

IF(librtlsdr_FOUND)
  MESSAGE(STATUS "librtlsdr found ; will compile Radio plugin... (PLUGIN)")
ENDIF(librtlsdr_FOUND)

IF(gupnp_FOUND)
  MESSAGE(STATUS "gupnp found ; will compile Media plugin... (PLUGIN)")
ENDIF(gupnp_FOUND)

INCLUDE(FindThreads)
FIND_PACKAGE(Threads)

SET(include_dirs ${INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include ${json-c_INCLUDE_DIRS} ${libmicrohttpd_INCLUDE_DIRS} ${uuid_INCLUDE_DIRS} ${dbus_INCLUDE_DIRS} ${alsa_INCLUDE_DIRS} ${pulseaudio_INCLUDE_DIRS} ${librtlsdr_INCLUDE_DIRS} ${gupnp_INCLUDE_DIRS} ${openssl_INCLUDE_DIRS})
SET(link_libraries ${json-c_LIBRARIES} ${libmicrohttpd_LIBRARIES} ${uuid_LIBRARIES} ${dbus_LIBRARIES} ${alsa_LIBRARIES} ${pulseaudio_LIBRARIES} ${librtlsdr_LIBRARIES} ${gupnp_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${libefence_LIBRARIES} ${openssl_LIBRARIES} -lmagic -lm -ldl -lrt)
SET(plugin_install_dir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/afb)

ADD_DEFINITIONS(-DPLUGIN_INSTALL_DIR="${plugin_install_dir}")

ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(plugins)

ADD_EXECUTABLE(afb-daemon $<TARGET_OBJECTS:src>)
INCLUDE_DIRECTORIES(${include_dirs})
TARGET_LINK_LIBRARIES(afb-daemon ${link_libraries})

INSTALL(TARGETS afb-daemon
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
