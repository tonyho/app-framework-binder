
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
	afb-sig-handler.c
	afb-websock.c
	afb-ws-client.c
	afb-ws-json1.c
	afb-ws.c
	afb-wsj1.c
	main.c
	session.c
	verbose.c
	websock.c
)

INCLUDE_DIRECTORIES(${include_dirs})

