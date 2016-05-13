
ADD_LIBRARY(afb-lib OBJECT
	afb-api-dbus.c
	afb-api-so.c
	afb-apis.c
	afb-common.c
	afb-context.c
	afb-hreq.c
	afb-hsrv.c
	afb-hswitch.c
	afb-method.c
	afb-msg-json.c
	afb-sig-handler.c
	afb-websock.c
	afb-ws-client.c
	afb-ws-json1.c
	afb-ws.c
	afb-wsj1.c
	session.c
	verbose.c
	websock.c
)


INCLUDE_DIRECTORIES(${include_dirs})

ADD_EXECUTABLE(afb-daemon $<TARGET_OBJECTS:afb-lib> main.c)
INCLUDE_DIRECTORIES(${include_dirs})
TARGET_LINK_LIBRARIES(afb-daemon ${link_libraries})

ADD_EXECUTABLE(afb-client-demo $<TARGET_OBJECTS:afb-lib> afb-client-demo.c)
INCLUDE_DIRECTORIES(${include_dirs})
TARGET_LINK_LIBRARIES(afb-client-demo ${link_libraries})

INSTALL(TARGETS afb-daemon
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
