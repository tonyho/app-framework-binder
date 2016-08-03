
INCLUDE(FindPkgConfig)

ADD_DEFINITIONS(-DBINDING_INSTALL_DIR="${binding_install_dir}")

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

PKG_CHECK_MODULES(libsystemd REQUIRED libsystemd>=222)
PKG_CHECK_MODULES(libmicrohttpd REQUIRED libmicrohttpd>=0.9.48)
PKG_CHECK_MODULES(openssl REQUIRED openssl)
PKG_CHECK_MODULES(uuid REQUIRED uuid)

INCLUDE_DIRECTORIES(
	${include_dirs}
	${libsystemd_INCLUDE_DIRS}
	${libmicrohttpd_INCLUDE_DIRS}
	${uuid_INCLUDE_DIRS}
	${openssl_INCLUDE_DIRS}
)

ADD_LIBRARY(afb-lib STATIC
	afb-api-dbus.c
	afb-api-so.c
	afb-api-ws.c
	afb-apis.c
	afb-common.c
	afb-context.c
	afb-evt.c
	afb-hreq.c
	afb-hsrv.c
	afb-hswitch.c
	afb-method.c
	afb-msg-json.c
	afb-sig-handler.c
	afb-svc.c
	afb-subcall.c
	afb-websock.c
	afb-ws-client.c
	afb-ws-json1.c
	afb-ws.c
	afb-wsj1.c
	session.c
	verbose.c
	websock.c
)

###########################################
# build and install afb-daemon
###########################################
ADD_EXECUTABLE(afb-daemon main.c)
TARGET_LINK_LIBRARIES(afb-daemon
	afb-lib
	${link_libraries}
	${libsystemd_LIBRARIES}
	${libmicrohttpd_LIBRARIES}
	${uuid_LIBRARIES}
	${openssl_LIBRARIES}
	-lmagic
	-ldl
	-lrt
)
INSTALL(TARGETS afb-daemon
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

###########################################
# build and install libafbwsc
###########################################
ADD_LIBRARY(afbwsc SHARED afb-ws.c afb-ws-client.c afb-wsj1.c websock.c)
SET_TARGET_PROPERTIES(afbwsc PROPERTIES
	VERSION ${LIBAFBWSC_VERSION}
	SOVERSION ${LIBAFBWSC_SOVERSION})
TARGET_LINK_LIBRARIES(afbwsc
	${libsystemd_LIBRARIES}
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-afbwsc.map
	-Wl,--as-needed
	-Wl,--gc-sections
)
INSTALL(TARGETS afbwsc LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
INSTALL(FILES afb-wsj1.h afb-ws-client.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/afb)

###########################################
# build and install afb-client-demo
###########################################
ADD_EXECUTABLE(afb-client-demo afb-client-demo.c)
TARGET_LINK_LIBRARIES(afb-client-demo
	afbwsc
	${link_libraries}
	${libsystemd_LIBRARIES}
)
INSTALL(TARGETS afb-client-demo
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})


