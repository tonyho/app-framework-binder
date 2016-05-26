HOWTO WRITE a PLUGIN for AFB-DAEMON
===================================
    version: 1
    Date:    25 May 2016
    Author:  José Bollo

TABLE-OF-CONTENT-HERE

Summary
-------

The binder afb-daemon serves files through
the HTTP protocol and offers access to API's through
HTTP or WebSocket protocol.

The plugins are used to add API's to afb-daemon.
This part describes how to write a plugin for afb-daemon.
Excepting this summary, this part is intended to be read
by developpers.

Before going into details, through a tiny example,
a short overview plugins basis is needed.

### Nature of a plugin

A plugin is a separate piece of code made of a shared library.
The plugin is loaded and activated by afb-daemon when afb-daemon
starts.

Technically, a plugin is not linked to any library of afb-daemon.

### Kinds of plugins

There is two kinds of plugins: application plugins and service
plugins.

#### Application plugins

Application plugins are intended to be instanciated for each
application: when an application using that plugin is started,
its binder starts a new instance of the plugin.

It means that the application plugins mainly have only one
context to manage for one client.

#### Service plugins

Service plugins are intended to be instanciated only one time
only and connected to many clients.

So either it does not manage context at all or otherwise,
if it manages context, it should be able to manage one context
per client.

In details, it may be useful to have service plugins at a user
level.
 
### Live cycle of a plugin within afb-daemon

The plugins are loaded and activated when afb-daemon starts.

At start, the plugin initialise itself.
If it fails to initialise then afb-daemon stops.

Conversely, if it success to initialize, it must declare
a name, that must be unique, and a list of API's verbs.

When initialized, the functions implementing the API's verbs
of the plugin are activated on call.

At the end, nothing special is done by afb-daemon.
Consequently, developpers of plugins should use 'atexit'
or 'on_exit' during initialisation if they need to
perform specific actions when stopping.

### Content of a plugin

For afb-daemon, a plugin contains 2 different
things: names and functions.

There is two kind of names:
 - the name of the plugin,
 - the names of the verbs.

There is two kind of functions:
 - the initialisation function
 - functions implementing verbs

Afb-daemon translates the name of the method that is
invoked to a pair of API and verb names. For example,
the method named **foo/bar** translated to the API
name **foo** and the verb name **bar**.
To serve it, afb-daemon search the plugin that record
the name **foo** and if it also recorded the verb **bar**,
it calls the implementation function declared for this verb.

Afb-daemon make no distinction between lower case
and upper case when searching for a method.
Thus, The names **TicTacToe/Board** and **tictactoe/borad**
are equals.

#### The name of the plugin

The name of the plugin is also known as the name
of the API that defines the plugin.

This name is also known as the prefix.

The name of a plugin MUST be unique within afb-daemon.

For example, when a client of afb-daemon
calls a method named **foo/bar**. Afb-daemon
extracts the prefix **foo** and the suffix **bar**.
**foo** is the API name and must match a plugin name,
the plugin that implements the verb **bar**.

#### Names of verbs

Each plugin exposes a set of verbs that can be called
by client of afb-daemon.

The name of a verb MUST be unique within a plugin.

Plugins link verbs to functions that are called
when clients emit requests for that verb.

For example, when a client of afb-daemon
calls a method named **foo/bar**.

#### The initialisation function

The initialisation function serves several purposes.

1. It allows afb-daemon to check the version
of the plugin using the name of the initialisation
functions that it found. Currently, the initialisation
function is named **pluginAfbV1Register**. It identifies
the first version of plugins.

2. It allows the plugin to initialise itself.

3. It serves to the plugin to declare names, descriptions,
requirements and implmentations of the verbs that it exposes.

#### Functions implementing verbs

When a method is called, afb-daemon constructs a request
object and pass it to the implementation function for verb
within the plugin of the API.

An implementation function receives a request object that
is used to get arguments of the request, to send
answer, to store session data.

A plugin MUST send an answer to the request.

But it is not mandatory to send the answer
before to return from the implementing function.
This behaviour is important for implementing
asynchronous actions.

Implementation functions that always reply to the request
before returning are named *synchronous implementations*.
Those that don't always reply to the request before
returning are named *asynchronous implementations*.

Asynchronous implementations typically initiate an
asynchronous action and record to send the reply
on completion of this action.

The Tic-Tac-Toe example
-----------------------

This part explains how to write an afb-plugin.
For the sake of being practical we will use many
examples from the tic-tac-toe example.
This plugin example is in *plugins/samples/tic-tac-toe.c*.

This plugin is named ***tictactoe***.

Choosing names
--------------

The designer of a plugin must defines names for its plugin
(or its API) and for the verbs of its API. He also
must defines names for arguments given by name.

While forging names, the designer should take into account
the rules for making valid names and some rules that make
the names easy to use across plaforms.

The names and strings used ALL are UTF-8 encoded.

### Names for API (plugin)

The names of the API are checked.
All characters are authorised except:

- the control characters (\u0000 .. \u001f)
- the characters of the set { ' ', '"', '#', '%', '&',
  '\'', '/', '?', '`', '\x7f' }

In other words the set of forbidden characters is
{ \u0000..\u0020, \u0022, \u0023, \u0025..\u0027,
  \u002f, \u003f, \u0060, \u007f }.

Afb-daemon make no distinction between lower case
and upper case when searching for an API by its name.

### Names for verbs

The names of the verbs are not checked.

However, the validity rules for verb's names are the
same as for API's names except that the dot (.) character
is forbidden.

Afb-daemon make no distinction between lower case
and upper case when searching for an API by its name.

### Names for arguments

The names for arguments are not restricted and can be
anything.

The arguments are searched with the case sensitive
string comparison. Thus the names "index" and "Index"
are not the same.

### Forging names widely available

The key names of javascript object can be almost
anything using the arrayed notation:

	object[key] = value

That is not the case with the dot notation:

	object.key = value

Using the dot notation, the key must be a valid javascript
identifier.

For this reason, the chosen names should better be
valid javascript identifier.

It is also a good practice, even for arguments, to not
rely on the case sensitivity and to avoid the use of
names different only by the case.

Options to set when compiling plugins
-------------------------------------

Afb-daemon provides a configuration file for *pkg-config*.
Typing the command

	pkg-config --cflags afb-daemon

will print the flags to use for compiling, like this:

	$ pkg-config --cflags afb-daemon
	-I/opt/local/include -I/usr/include/json-c 

For linking, you should use

	$ pkg-config --libs afb-daemon
	-ljson-c

As you see, afb-daemon automatically includes dependency to json-c.
This is done through the **Requires** keyword of pkg-config.

If this behaviour is a problem, let us know.

Header files to include
-----------------------

The plugin *tictactoe* has the following lines for its includes:

	#define _GNU_SOURCE
	#include <stdio.h>
	#include <string.h>
	#include <json-c/json.h>
	#include <afb/afb-plugin.h>

The header *afb/afb-plugin.h* includes all the features that a plugin
needs except two foreign header that must be included by the plugin
if it needs it:

- *json-c/json.h*: this header must be include to handle json objects;
- *systemd/sd-event.h*: this must be include to access the main loop;
- *systemd/sd-bus.h*: this may be include to use dbus connections.

The *tictactoe* plugin does not use systemd features so it is not included.

When including *afb/afb-plugin.h*, the macro **_GNU_SOURCE** must be
defined.

Writing a synchronous verb implementation
-----------------------------------------

The verb **tictactoe/board** is a synchronous implementation.
Here is its listing:

	/*
	 * get the board
	 */
	static void board(struct afb_req req)
	{
		struct board *board;
		struct json_object *description;

		/* retrieves the context for the session */
		board = board_of_req(req);
		INFO(afbitf, "method 'board' called for boardid %d", board->id);

		/* describe the board */
		description = describe(board);

		/* send the board's description */
		afb_req_success(req, description, NULL);
	}

This examples show many aspects of writing a synchronous
verb implementation. Let summarize it:

1. The function **board_of_req** retrieves the context stored
for the plugin: the board.

2. The macro **INFO** sends a message of kind *INFO*
to the logging system. The global variable named **afbitf**
used represents the interface to afb-daemon.

3. The function **describe** creates a json_object representing
the board.

4. The function **afb_req_success** sends the reply, attaching to
it the object *description*.

### The incoming request

For any implementation, the request is received by a structure of type
**struct afb_req**.

> Note that this is a PLAIN structure, not a pointer to a structure.

The definition of **struct afb_req** is:

	/*
	 * Describes the request by plugins from afb-daemon
	 */
	struct afb_req {
		const struct afb_req_itf *itf;	/* the interfacing functions */
		void *closure;			/* the closure for functions */
	};

It contains two pointers: one, *itf*, points to the functions needed
to handle the internal request represented by the second pointer, *closure*.

> The structure must never be used directly.
> Insted, use the intended functions provided
> by afb-daemon and described here.

*req* is used to get arguments of the request, to send
answer, to store session data.

This object and its interface is defined and documented
in the file names *afb/afb-req-itf.h*

The above example uses 2 times the request object *req*.

The first time, it is used for retrieving the board attached to
the session of the request.

The second time, it is used to send the reply: an object that
describes the current board.

### Associating a context to the session

When the plugin *tic-tac-toe* receives a request, it musts regain
the board that describes the game associated to the session.

For a plugin, having data associated to a session is a common case.
This data is called the context of the plugin for the session.
For the plugin *tic-tac-toe*, the context is the board.

The requests *afb_req* offer four functions for
storing and retrieving the context associated to the session.

These functions are:

- **afb_req_context_get**:
  retrieves the context data stored for the plugin.

- **afb_req_context_set**:
  store the context data of the plugin.

- **afb_req_context**:
  retrieves the context data of the plugin,
  if needed, creates the context and store it.

- **afb_req_context_clear**:
  reset the stored data.

The plugin *tictactoe* use a convenient function to retrieve
its context: the board. This function is *board_of_req*:

	/*
	 * retrieves the board of the request
	 */
	static inline struct board *board_of_req(struct afb_req req)
	{
		return afb_req_context(req, (void*)get_new_board, (void*)release_board);
	}

The function **afb_req_context** ensure an existing context
for the session of the request.
Its two last arguments are functions. Here, the casts are required
to avoid a warning when compiling.

Here is the definition of the function **afb_req_context**

	/*
	 * Gets the pointer stored by the plugin for the session of 'req'.
	 * If the stored pointer is NULL, indicating that no pointer was
	 * already stored, afb_req_context creates a new context by calling
	 * the function 'create_context' and stores it with the freeing function
	 * 'free_context'.
	 */
	static inline void *afb_req_context(struct afb_req req, void *(*create_context)(), void (*free_context)(void*))
	{
		void *result = afb_req_context_get(req);
		if (result == NULL) {
			result = create_context();
			afb_req_context_set(req, result, free_context);
		}
		return result;
	}

The second argument if the function that creates the context.
For the plugin *tic-tac-toe* it is the function **get_new_board**.
The function **get_new_board** creates a new board and set its
count of use to 1. The boards are counting their count of use
to free there ressources when no more used.

The third argument if the function that frees the context.
For the plugin *tic-tac-toe* it is the function **release_board**.
The function **release_board** decrease the the count of use of
the board given as argument. If the use count decrease to zero,
the board data are freed.

The definition of the other functions for dealing with contexts are:

	/*
	 * Gets the pointer stored by the plugin for the session of 'req'.
	 * When the plugin has not yet recorded a pointer, NULL is returned.
	 */
	void *afb_req_context_get(struct afb_req req);

	/*
	 * Stores for the plugin the pointer 'context' to the session of 'req'.
	 * The function 'free_context' will be called when the session is closed
	 * or if plugin stores an other pointer.
	 */
	void afb_req_context_set(struct afb_req req, void *context, void (*free_context)(void*));

	/*
	 * Frees the pointer stored by the plugin for the session of 'req'
	 * and sets it to NULL.
	 *
	 * Shortcut for: afb_req_context_set(req, NULL, NULL)
	 */
	static inline void afb_req_context_clear(struct afb_req req)
	{
		afb_req_context_set(req, NULL, NULL);
	}

### Sending the reply to a request

Two kinds of replies can be made: successful replies and
failure replies.

> Sending a reply to a request must be done at most one time.

The two functions to send a reply of kind "success" are
**afb_req_success** and **afb_req_success_f**.

	/*
	 * Sends a reply of kind success to the request 'req'.
	 * The status of the reply is automatically set to "success".
	 * Its send the object 'obj' (can be NULL) with an
	 * informationnal comment 'info (can also be NULL).
	 */
	void afb_req_success(struct afb_req req, struct json_object *obj, const char *info);

	/*
	 * Same as 'afb_req_success' but the 'info' is a formatting
	 * string followed by arguments.
	 */
	void afb_req_success_f(struct afb_req req, struct json_object *obj, const char *info, ...);

The two functions to send a reply of kind "failure" are
**afb_req_fail** and **afb_req_fail_f**.

	/*
	 * Sends a reply of kind failure to the request 'req'.
	 * The status of the reply is set to 'status' and an
	 * informationnal comment 'info' (can also be NULL) can be added.
	 *
	 * Note that calling afb_req_fail("success", info) is equivalent
	 * to call afb_req_success(NULL, info). Thus even if possible it
	 * is strongly recommanded to NEVER use "success" for status.
	 */
	void afb_req_fail(struct afb_req req, const char *status, const char *info);

	/*
	 * Same as 'afb_req_fail' but the 'info' is a formatting
	 * string followed by arguments.
	 */
	void afb_req_fail_f(struct afb_req req, const char *status, const char *info, ...);

Getting argument of invocation
------------------------------

Many verbs expect arguments. Afb-daemon let plugins
retrieve their arguments by name not by position.

Arguments are given by the requests either through HTTP
or through WebSockets.

For example, the verb **join** of the plugin **tic-tac-toe**
expects one argument: the *boardid* to join. Here is an extract:

	/*
	 * Join a board
	 */
	static void join(struct afb_req req)
	{
		struct board *board, *new_board;
		const char *id;

		/* retrieves the context for the session */
		board = board_of_req(req);
		INFO(afbitf, "method 'join' called for boardid %d", board->id);

		/* retrieves the argument */
		id = afb_req_value(req, "boardid");
		if (id == NULL)
			goto bad_request;
		...

The function **afb_req_value** search in the request *req*
for an argument whose name is given. When no argument of the
given name was passed, **afb_req_value** returns NULL.

> The search is case sensitive. So the name *boardid* is not the
> same name than *BoardId*. But this must not be assumed so two
> expected names of argument should not differ only by case.

### Basic functions for querying arguments

The function **afb_req_value** is defined as below:

	/*
	 * Gets from the request 'req' the string value of the argument of 'name'.
	 * Returns NULL if when there is no argument of 'name'.
	 * Returns the value of the argument of 'name' otherwise.
	 *
	 * Shortcut for: afb_req_get(req, name).value
	 */
	static inline const char *afb_req_value(struct afb_req req, const char *name)
	{
		return afb_req_get(req, name).value;
	}

It is defined as a shortcut to call the function **afb_req_get**.
That function is defined as below:

	/*
	 * Gets from the request 'req' the argument of 'name'.
	 * Returns a PLAIN structure of type 'struct afb_arg'.
	 * When the argument of 'name' is not found, all fields of result are set to NULL.
	 * When the argument of 'name' is found, the fields are filled,
	 * in particular, the field 'result.name' is set to 'name'.
	 *
	 * There is a special name value: the empty string.
	 * The argument of name "" is defined only if the request was made using
	 * an HTTP POST of Content-Type "application/json". In that case, the
	 * argument of name "" receives the value of the body of the HTTP request.
	 */
	struct afb_arg afb_req_get(struct afb_req req, const char *name);

That function takes 2 parameters: the request and the name
of the argument to retrieve. It returns a PLAIN structure of
type **struct afb_arg**.

There is a special name that is defined when the request is
of type HTTP/POST with a Content-Type being application/json.
This name is **""** (the empty string). In that case, the value
of this argument of empty name is the string received as a body
of the post and is supposed to be a JSON string.

The definition of **struct afb_arg** is:

	/*
	 * Describes an argument (or parameter) of a request
	 */
	struct afb_arg {
		const char *name;	/* name of the argument or NULL if invalid */
		const char *value;	/* string representation of the value of the argument */
					/* original filename of the argument if path != NULL */
		const char *path;	/* if not NULL, path of the received file for the argument */
					/* when the request is finalized this file is removed */
	};

The structure returns the data arguments that are known for the
request. This data include a field named **path**. This **path**
can be accessed using the function **afb_req_path** defined as
below:

	/*
	 * Gets from the request 'req' the path for file attached to the argument of 'name'.
	 * Returns NULL if when there is no argument of 'name' or when there is no file.
	 * Returns the path of the argument of 'name' otherwise.
	 *
	 * Shortcut for: afb_req_get(req, name).path
	 */
	static inline const char *afb_req_path(struct afb_req req, const char *name)
	{
		return afb_req_get(req, name).path;
	}

The path is only defined for HTTP/POST requests that send file.

### Arguments for received files

As it is explained just above, clients can send files using
HTTP/POST requests.

Received files are attached to a arguments. For example, the
following HTTP fragment (from test/sample-post.html)
will send an HTTP/POST request to the method
**post/upload-image** with 2 arguments named *file* and
*hidden*.

	<h2>Sample Post File</h2>
	<form enctype="multipart/form-data">
	    <input type="file" name="file" />
	    <input type="hidden" name="hidden" value="bollobollo" />
	    <br>
	    <button formmethod="POST" formaction="api/post/upload-image">Post File</button>
	</form>

In that case, the argument named **file** has its value and its
path defined and not NULL.

The value is the name of the file as it was
set by the HTTP client and is generally the filename on the
client side.

The path is the path of the file saved on the temporary local storage
area of the application. This is a randomly generated and unic filename
not linked in any way with the original filename on the client.

The plugin can use the file at the given path the way that it wants:
read, write, remove, copy, rename...
But when the reply is sent and the query is terminated, the file at
this path is destroyed if it still exist.

### Arguments as a JSON object

Plugins can get all the arguments as one single object.
This feature is provided by the function **afb_req_json**
that is defined as below:

	/*
	 * Gets from the request 'req' the json object hashing the arguments.
	 * The returned object must not be released using 'json_object_put'.
	 */
	struct json_object *afb_req_json(struct afb_req req);

It returns a json object. This object depends on how the request was
made:

- For HTTP requests, this is an object whose keys are the names of the
arguments and whose values are either a string for common arguments or
an object like { "file": "...", "path": "..." }

- For WebSockets requests, the returned object is the object
given by the client transparently transported.

> In fact, for Websockets requests, the function **afb_req_value**
> can be seen as a shortcut to
> *json_object_get_string(json_object_object_get(afb_req_json(req), name))*

Sending messages to the log system
----------------------------------

How to build a plugin
---------------------

Afb-daemon provides a *pkg-config* configuration file.


