HOWTO WRITE an APPLICATION above AGL FRAMEWORK
==============================================
    version: 1
    Date:    29 mai 2016
    Author:  José Bollo

TABLE-OF-CONTENT-HERE


Writing a C application
-----------------------

C applications can use the binder afb-daemon through a websocket connection.

The library **libafbwsc** is made for C clients that want
to connect to the afb-daemon binder.

The program **afb-client-demo** is the C program that use
the provided library **libafbwsc**.
Its source code is here
[src/afb-client-demo.c](https://github.com/iotbzh/afb-daemon/blob/master/src/afb-client-demo.c).

The current implementation use libsystemd and file descriptors.
This may be changed in the future to also support secure sockets
and being less dependant of libsystemd.


