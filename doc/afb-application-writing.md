HOWTO WRITE an APPLICATION above AGL FRAMEWORK
==============================================
    version: 1
    Date:    30 mai 2016
    Author:  José Bollo

TABLE-OF-CONTENT-HERE

Languages for writing Applications
----------------------------------

### Writing an HTML5 application

Developpers of HTML5 applications (client side) can easyly create
applications for AGL framework using their prefered
HTML framework.

Developpers can also create powerful server side plugins to improve
their application. This server side plugin should return the mime-type
application/json and can be accessed either by HTTP or by Websockets.

In a near future, the JSON-RPC protocol will be available together
with the current x-afb-json1 protocol.

Two examples of HTML5 applications are given:

- [afb-client](https://github.com/iotbzh/afb-client) a simple "hello world" application

- [afm-client](https://github.com/iotbzh/afm-client) a simple "Home screen" application

### Writing a Qt application

Writing Qt applications is also possible because Qt offers APIs to
make HTTP queries and to connect using WebSockets.

It is even possible to write a QML application.
It is demontrated by the sample application token-websock:

- [token-websock](https://github.com/iotbzh/afb-daemon/blob/master/test/token-websock.qml) 
a simple "hello world" application in QML

### Writing a C application

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

Handling sessions within applications
-------------------------------------

Applications must be aware of the the features session and token
when they interact with the binder afb-daemon.

Applications are communicating with their binder afb-daemon using
a network connection or a kind of network connection (unix domain
socket isn't currently implemented but could be used in near future).
Also, HTTP protocol is not a connected protocol. It means that
the socket connection can not be used to authenticate a client.

For this reason, the binder should authenticate the application
by using a commonly shared secret named token and the identification
of the client named session.

### Handling sessions

Plugins and features of the binder need to keep track of the client
instances. This of importance for plugins running as service
because they may have to separate the data of each client.

For common HTML5 browser running an HTML5 application.

### Exchanging tokens

At start, the framework communicates a common secret to both the binder
and its client: the application. This initial secret is the
initial token.

For each of its client application, the binder manages a current active
token. The initial token is the default active token. It is the expected
token for new clients.



