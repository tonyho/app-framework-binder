
ADD_LIBRARY(src OBJECT
	main.c
	config.c
	session.c
	http-svc.c
	rest-api.c
	afb-method.c
	helper-api.c)

INCLUDE_DIRECTORIES(${include_dirs})

