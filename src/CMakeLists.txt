
ADD_LIBRARY(src OBJECT
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
	afb-websock.c
	afb-ws-json.c
	afb-ws.c
	main.c
	session.c
	verbose.c
	websock.c
)

INCLUDE_DIRECTORIES(${include_dirs})

