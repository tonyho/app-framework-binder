
ADD_LIBRARY(src OBJECT
	main.c
	session.c
	http-svc.c
	afb-rest-api.c
	afb-apis.c
	afb-method.c
	afb-hreq.c
	afb-websock.c
	websock.c
	helper-api.c)

INCLUDE_DIRECTORIES(${include_dirs})

