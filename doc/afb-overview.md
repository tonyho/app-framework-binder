Overview of AFB-DAEMON
======================
    version: 1
    Date:    30 mai 2016
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

<a id="binder-fig-basis"><h4>Figure: binder afb-daemon, basis</h4></a>

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
	.   |        b i n d e r         :             |  .
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

Use cases of the binder afb-daemon
----------------------------------

This section tries to give a better understanding of the binder
usage through several use cases.

### Remotely running application

One of the most interresting aspect of using the binder afb-daemon
is the ability to run applications remotely. This feature is
possible because the binder afb-daemon implements native web
protocols.

So the [figure binder, basis](#binder-fig-1) would become
when the application is run remotely:

<a id="binder-fig-remote"><h4>Figure: binder afb-daemon and remotely running application</h4></a>

	             +------------------------------+
	             |                              |
	             |    A P P L I C A T I O N     |
	             |                              |
	             +--------------+---------------+
	                            |
	                       ~ ~ ~ ~ ~ ~
	                      :  NETWORK  :
	                       ~ ~ ~ ~ ~ ~
	                            |
	. . . . . . . . . . . . . . | . . . . . . . . . . . . . .
	. Isolated security         |                           .
	.   context                 |                           .
	.                           |                           .
	.     . . . . . . . . . . . . . . . . . . . . . . . .   .
	.     .                                             .   .
	.     .               F I R E W A L L               .   .
	.     .                                             .   .
	.     . . . . . . . . . . . . . . . . . . . . . . . .   .
	.                           |                           .
	.       +-------------------+----------------------+    .
	.       |                            :             |    .
	.       |    A F B - D A E M O N     :   PLUGINS   |    .
	.       |                            :             |    .
	.       +-------------------+----------------------+    .
	.                           |                           .
	. . . . . . . . . . . . . . | . . . . . . . . . . . . . .
	                            |
	                            v
	                       AGL SYSTEM

### Adding native features to HTML5/QML applications

Applications can provide with their packaged delivery a plugin.
That plugin will be instanciated for each application instance.
The methods of the plugin will be accessible by applications and
will be excuted within the security context.

### Offering services to the system

It is possible to run the binder afb-daemon as a daemon that provides the
API of its plugins.

This will be used for:

1. offering common APIs

2. provide application's services (services provided as application)

In that case, the figure showing the whole aspects is

<a id="binder-fig-remote"><h4>Figure: binder afb-daemon for services</h4></a>

	. . . . . . . . . . . . . . . . . . . . . . 
	.  Isolated security context application  . 
	.                                         . 
	.    +------------------------------+     . 
	.    |                              |     . 
	.    |    A P P L I C A T I O N     |     . 
	.    |                              |     . 
	.    +--------------+---------------+     .     . . . . . . . . . . . . . . . . . . . . . .
	.                   |                     .     .        Isolated security context A      .
	.                   |                     .     .                                         .
	. +-----------------+------------------+  .     . +------------------------------------+  .
	. |                        :           |  .     . |                        :           |  .
	. |      b i n d e r       :           |  .     . |      b i n d e r       :  service  |  .
	. |  A F B - D A E M O N   :  PLUGINS  |  .     . |  A F B - D A E M O N   :  PLUGINS  |  .
	. |                        :           |  .     . |                        :     A     |  .
	. +-----------------+------------------+  .     . +-----------------+------------------+  .
	.                   |                     .     .                   |                     .
	. . . . . . . . . . | . . . . . . . . . . .     . . . . . . . . . . | . . . . . . . . . . .
	                    |                                               |
	                    v                                               v
	         ================================================================================
	                                     D - B U S   &   C Y N A R A
	         ================================================================================
	                    ^                                               ^
	                    |                                               |
	. . . . . . . . . . | . . . . . . . . . . .     . . . . . . . . . . | . . . . . . . . . . .
	.                   |                     .     .                   |                     .
	. +-----------------+------------------+  .     . +-----------------+------------------+  .
	. |                        :           |  .     . |                        :           |  .
	. |      b i n d e r       :  service  |  .     . |      b i n d e r       :  service  |  .
	. |  A F B - D A E M O N   :  PLUGINS  |  .     . |  A F B - D A E M O N   :  PLUGINS  |  .
	. |                        :     B     |  .     . |                        :     C     |  .
	. +------------------------------------+  .     . +------------------------------------+  .
	.                                         .     .                                         .
	.        Isolated security context B      .     .        Isolated security context C      .
	. . . . . . . . . . . . . . . . . . . . . .     . . . . . . . . . . . . . . . . . . . . . .


The plugins of the binder afb-daemon
------------------------------------

The binder can instanciate plugins. The primary use of plugins
is to add native methods that can be accessed by applications
written with any language through web technologies ala JSON RPC.

This simple idea is declined to serves multiple purposes:

1. add native feature to applications

2. add common API available by any applications

3. provide customers services

A specific document shows 
