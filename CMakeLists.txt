PROJECT(afb-daemon C)

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.8)
SET(CMAKE_BUILD_TYPE Debug)
SET(CMAKE_POSITION_INDEPENDENT_CODE ON)

SET(PROJECT_NAME "AFB Daemon")
SET(PROJECT_PRETTY_NAME "Application Framework Binder Daemon")
SET(PROJECT_DESCRIPTION "Secured binder of API for clients of the Application framework")
SET(PROJECT_VERSION "1.0")
SET(PROJECT_URL "https://github.com/iotbzh/afb-daemon")

SET(LIBAFBWSC_VERSION "1.0")
SET(LIBAFBWSC_SOVERSION "1")

INCLUDE(FindPkgConfig)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(GNUInstallDirs)

###########################################################################

link_libraries(-Wl,--as-needed -Wl,--gc-sections)

add_compile_options(-Wall -Wextra -Wconversion)
add_compile_options(-Wno-unused-parameter) # frankly not using a parameter does it care?
add_compile_options(-Wno-sign-compare -Wno-sign-conversion)
add_compile_options(-Werror=maybe-uninitialized)
add_compile_options(-Werror=implicit-function-declaration)
add_compile_options(-ffunction-sections -fdata-sections)
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

SET(binding_install_dir ${CMAKE_INSTALL_FULL_LIBDIR}/afb)

ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(include)
ADD_SUBDIRECTORY(bindings)

############################################################
# installs the pkgconfig files
CONFIGURE_FILE(afb-daemon.pc.in afb-daemon.pc @ONLY)
CONFIGURE_FILE(libafbwsc.pc.in libafbwsc.pc @ONLY)

INSTALL(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/afb-daemon.pc
    ${CMAKE_CURRENT_BINARY_DIR}/libafbwsc.pc
    DESTINATION
    ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )

