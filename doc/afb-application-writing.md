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

The examples **token-websock.qml** and **afb-client** are demonstrating
how authentication and sessions are managed.

### Handling sessions

Plugins and features of the binder need to keep track of the client
instances. This of importance for plugins running as service
because they may have to separate the data of each client.

For HTML5 applications, the web runtime handles the cookie of session
that the binder afb-daemon automatically sets.

In any case, the session identifier can be set using the parameters
**uuid** or **x-afb-uuid** in the request uri. That is understood
by HTTP requests and by the negociation of websockets.

### Exchanging tokens

At start, the framework communicates a common secret to both the binder
and its client: the application. This initial secret is the
initial token.

For each of its client application, the binder manages a current active
token for the session. This authentication token can be a requirement for
accessing some methods.

The token must be passed in the request uri on HTTP or at connecting
websockets using the parameter **token** or **x-afb-token**.

To ensure security, tokens must be refreshed periodically.

### Example of session management

For the following exmples, we suppose that you launched **afb-daemon** like that or similar:

    $ afb-daemon --port=1234 --token=123456 [...]

with the expectation that the plugin **AuthLogin** is loaded.

#### Using curl

First, connects with the initial token, 123456:

    $ curl http://localhost:1234/api/auth/connect?token=123456
    {
      "jtype": "afb-reply",
      "request": {
         "status": "success",
         "token": "0aef6841-2ddd-436d-b961-ae78da3b5c5f",
         "uuid": "850c4594-1be1-4e9b-9fcc-38cc3e6ff015"
      },
      "response": {"token": "A New Token and Session Context Was Created"}
    }

It returns an answer containing the uuid of the session, 850c4594-1be1-4e9b-9fcc-38cc3e6ff015,
and the refreshed token, 850c4594-1be1-4e9b-9fcc-38cc3e6ff015.

Let check that it is available:

    $ curl http://localhost:1234/api/auth/check?token=0aef6841-2ddd-436d-b961-ae78da3b5c5f\&uuid=850c4594-1be1-4e9b-9fcc-38cc3e6ff015
    {
      "jtype": "afb-reply",
      "request": {"status":"success"},
      "response": {"isvalid":true}
    }

It works! So try now to refresh the token:

    $ curl http://localhost:1234/api/auth/refresh?token=0aef6841-2ddd-436d-b961-ae78da3b5c5f\&uuid=850c4594-1be1-4e9b-9fcc-38cc3e6ff015
    {
      "jtype": "afb-reply",
      "request": {
         "status":"success",
         "token":"b8ec3ec3-6ffe-448c-9a6c-efda69ad7bd9"
      },
      "response": {"token":"Token was refreshed"}
    }

Let now close the session:

    curl http://localhost:1234/api/auth/logout?token=b8ec3ec3-6ffe-448c-9a6c-efda69ad7bd9\&uuid=850c4594-1be1-4e9b-9fcc-38cc3e6ff015
    {
      "jtype": "afb-reply",
      "request": {"status": "success"},
      "response": {"info":"Token and all resources are released"}
    }

So now, checking for the uuid will be refused:

    curl http://localhost:1234/api/auth/check?token=b8ec3ec3-6ffe-448c-9a6c-efda69ad7bd9\&uuid=850c4594-1be1-4e9b-9fcc-38cc3e6ff015
    {
      "jtype": "afb-reply",
      "request": {
         "status": "failed",
         "info": "invalid token's identity"
      }
    }

#### Using afb-client-demo

Here is an example of exchange using **afb-client-demo**:

    $ afb-client-demo ws://localhost:1234/api?token=123456
    auth connect
    ON-REPLY 1:auth/connect: {"jtype":"afb-reply","request":{"status":"success",
       "token":"63f71a29-8b52-4f9b-829f-b3028ba46b68","uuid":"5fcc3f3d-4b84-4fc7-ba66-2d8bd34ae7d1"},
       "response":{"token":"A New Token and Session Context Was Created"}}
    auth check
    ON-REPLY 2:auth/check: {"jtype":"afb-reply","request":{"status":"success"},"response":{"isvalid":true}}
    auth refresh
    ON-REPLY 4:auth/refresh: {"jtype":"afb-reply","request":{"status":"success",
       "token":"8b8ba8f4-1b0c-48fa-962d-4a00a8c9157e"},"response":{"token":"Token was refreshed"}}
    auth check
    ON-REPLY 5:auth/check: {"jtype":"afb-reply","request":{"status":"success"},"response":{"isvalid":true}}
    auth refresh
    ON-REPLY 6:auth/refresh: {"jtype":"afb-reply","request":{"status":"success",
       "token":"e83b36f8-d945-463d-b983-5d8ed73ba529"},"response":{"token":"Token was refreshed"}}

Then you leave. And can reconnect as below:

    $ afb-client-demo ws://localhost:1234/api?token=e83b36f8-d945-463d-b983-5d8ed73ba529\&uuid=5fcc3f3d-4b84-4fc7-ba66-2d8bd34ae7d1 auth check
    ON-REPLY 1:auth/check: {"jtype":"afb-reply","request":{"status":"success"},"response":{"isvalid":true}}

The same can be continued using **curl**:

    $ curl http://localhost:1234/api/auth/check?token=e83b36f8-d945-463d-b983-5d8ed73ba529\&uuid=5fcc3f3d-4b84-4fc7-ba66-2d8bd34ae7d1
    {"jtype":"afb-reply","request":{"status":"success"},"response":{"isvalid":true}}

Format of replies
-----------------

The replies are made of one javascript object returned using JSON serialization.

This object containts at least 2 mandatory fields of name **jtype** and **request**
and an optionnal field of name **response**.

### Field jtype

The field **jtype** must have a value of type string equel to **"afb-reply"**.

### Field request

The field **request** must have a value of type object. This request object
has at least one field named **status** and four optionnal fields of name
**info**, **token**, **uuid**, **reqid**.

#### Subfield request.status

**status** must have a value of type string. This string is equal to **"success"**
only in case of success.

#### Subfield request.info

**info** is of type string and represent optionnal the information added to the reply.

#### Subfield request.token

**token** is of type string. It is sent either on the creation of the
session or when the token is refreshed.

#### Subfield request.uuid

**uuid** is of type string. It is sent on the creation of the session.

#### Subfield request.reqid

**reqid** is of type string. It is sent in response of HTTP requests
that added a parameter of name **reqid** or **x-afb-reqid**. The value
sent in the reply is the exact value received on the request.

### Field response

This field response optionnaly containts the object returned with successful replies.

### Template

This is a template of replies:

    {
      "jtype": "afb-reply",
      "request": {
           "status": "success",
           "info": "informationnal text",
           "token": "e83b36f8-d945-463d-b983-5d8ed73ba52",
           "uuid": "5fcc3f3d-4b84-4fc7-ba66-2d8bd34ae7d1",
           "reqid": "application-generated-id-23456"
         },
      "response": ....any response object....
    }

