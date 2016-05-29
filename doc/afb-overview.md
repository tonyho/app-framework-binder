Overview of AFB-DAEMON
======================
    version: 1
    Date:    29 mai 2016
    Author:  José Bollo

TABLE-OF-CONTENT-HERE

Roles of afb-daemon
-------------------

The name **afb-daemon** stands for *Application
Framework Binder Daemon*. That is why afb-daemon
is also named ***the binder***.

**Afb-daemon** is in charge to bind one instance of
an application to the AGL framework and AGL system.

On the following figure, you can use a typical use
of afb-daemon:

	. . . . . . . . . . . . . . . . . . . . . . . . . .
	.        Isolated security context                .
	.                                                 .
	.        +------------------------------+         .
	.        |                              |         .
	.        |    A P P L I C A T I O N     |         .
	.        |                              |         .
	.        +--------------+---------------+         .
	.                       |                         .
	.                       |                         .
	.   +-------------------+----------------------+  .
	.   |                            :             |  .
	.   |    A F B - D A E M O N     :   PLUGINS   |  .
	.   |                            :             |  .
	.   +-------------------+----------------------+  .
	.                       |                         .
	. . . . . . . . . . . . | . . . . . . . . . . . . .
	                        |
	                        v
	                   AGL SYSTEM

The application and its companion binder run in secured and isolated
environment set for them. Applications are intended to access to AGL
system through the binder.

The binder afb-daemon serves multiple purposes:

1. It acts as a gateway for the application to access the system;

2. It acts as an HTTP server for serving files to HTML5 applications;

3. It allows HTML5 applications to have native extensions subject
to security enforcement for accessing hardware ressources or
for speeding parts of algorithm.



