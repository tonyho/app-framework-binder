
ADD_LIBRARY(src OBJECT
	main.c
	session.c
	http-svc.c
	afb-apis.c
	afb-method.c
	afb-hreq.c
	afb-websock.c
	websock.c
	verbose.c)

INCLUDE_DIRECTORIES(${include_dirs})

