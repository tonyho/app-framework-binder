
ADD_LIBRARY(src OBJECT
	main.c
	session.c
	afb-hsrv.c
	afb-hswitch.c
	afb-apis.c
	afb-api-so.c
	afb-method.c
	afb-hreq.c
	afb-websock.c
	websock.c
	verbose.c
	utils-upoll.c
)

INCLUDE_DIRECTORIES(${include_dirs})

