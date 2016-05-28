HOWTO WRITE a PLUGIN for AFB-DAEMON
===================================
    version: 1
    Date:    27 mai 2016
    Author:  José Bollo

TABLE-OF-CONTENT-HERE

Summary
-------

The binder afb-daemon serves files through HTTP protocol
and offers to developers the capability to expose application APIs through
HTTP or WebSocket protocol.

Binder plugins are used to add API to afb-daemon.
This part describes how to write a plugin for afb-daemon.
Excepting this summary, this part is intended to be read
by developers.

Before moving further through an example, here after
a short overview of binder plugins fundamentals.

### Nature of a plugin

A plugin is an independent piece of software, self contain and expose as a dynamically loadable library.
A plugin is loaded by afb-daemon that exposes contained API dynamically at runtime.

Technically, a binder plugins does not reference and is not linked with any library from afb-daemon.

### Class of plugins

Application binder supports two kinds of plugins: application plugins and service
plugins. Technically both class of plugin are equivalent and coding API is shared. Only sharing mode and security context diverge.

#### Application-plugins

Application-plugins implements the glue in between application's UI and services. Every AGL application
has a corresponding binder that typically activates one or many plugins to interface the application logic with lower platform services.
When an application is started by AGL application framework, a dedicate binder is started that loads/activates application plugin(s). 
The API expose by application-plugin are executed within corresponding application security context.

Application plugins generally handle a unique context for a unique client. As the application framework start
a dedicated instance of afb_daemon for each AGL application, if a given plugin is used within multiple application each of those
application get a new and private instance of this "shared" plugin.

#### Service-plugins

Service-plugins enable API activation within corresponding service security context and not within calling application context. 
Service-plugins are intended to run as a unique instance that is shared in between multiple clients.

Service-plugins can either be stateless or manage client context. When managing context each client get a private context.

Sharing may either be global to the platform (ie: GPS service) or dedicated to a given user (ie: preference management)
 
### Live cycle of plugins within afb-daemon

Application and service plugins are loaded and activated each time a new afb-daemon is started.

At launch time, every loaded plugin initialise itself.
If a single plugin initialisation fail corresponding instance of afb-daemon self aborts.

Conversely, when plugin initialisation succeeds, it should register 
its unique name and the list of API verbs it exposes.

When initialised, on request from clients plugin's function corresponding to expose API verbs
are activated by the afb-daemon instance attached to the application or service.

At exit time, no special action is enforced by afb-daemon. When a specific actions is required at afb-daemon stop,
developers should use 'atexit/on_exit' during plugin initialisation sequence to register a custom exit function.

### Plugin Contend

Afb-daemon's plugin register two classes of objects: names and functions.

Plugins declare categories of names:
 - A unique plugin name,
 - Multiple API verb's names.

Plugins declare two categories of functions:
 - initialisation function
 - API functions implementing verbs

Afb-daemon parses URI requests to extract plugin name and API verb.
As an example, URI **foo/bar** translates to API verb named **bar** within plugin named **foo**.
To serve such a request, afb-daemon looks for an active plugin named **foo** and then within this plugin for an API verb named **bar**.
When find afb-daemon calls corresponding function with attached parameter if any.

Afb-daemon ignores letter case when parsing URI. Thus **TicTacToe/Board** and **tictactoe/borad** are equivalent.

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

Dependencies when compiling
---------------------------

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
This is done through the **Requires** keyword of pkg-config
because almost all plugin will use **json-c**.

If this behaviour is a problem, let us know.

Internally, afb-daemon uses **libsystemd** for its event loop
and for its binding to D-Bus.
Plugins developpers are encouraged to also use this library.
But it is a matter of choice.
Thus there is no dependency to **libsystemd**.

> Afb-daemon provides no library for plugins.
> The functions that the plugin need to have are given
> to the plugin at runtime through pointer using read-only
> memory.

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
same as for API names except that the dot (.) character
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
	 *
	 * For conveniency, the function calls 'json_object_put' for 'obj'.
	 * Thus, in the case where 'obj' should remain available after
	 * the function returns, the function 'json_object_get' shall be used.
	 */
	void afb_req_success(struct afb_req req, struct json_object *obj, const char *info);

	/*
	 * Same as 'afb_req_success' but the 'info' is a formatting
	 * string followed by arguments.
	 *
	 * For conveniency, the function calls 'json_object_put' for 'obj'.
	 * Thus, in the case where 'obj' should remain available after
	 * the function returns, the function 'json_object_get' shall be used.
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
	 *
	 * For conveniency, the function calls 'json_object_put' for 'obj'.
	 * Thus, in the case where 'obj' should remain available after
	 * the function returns, the function 'json_object_get' shall be used.
	 */
	void afb_req_fail(struct afb_req req, const char *status, const char *info);

	/*
	 * Same as 'afb_req_fail' but the 'info' is a formatting
	 * string followed by arguments.
	 *
	 * For conveniency, the function calls 'json_object_put' for 'obj'.
	 * Thus, in the case where 'obj' should remain available after
	 * the function returns, the function 'json_object_get' shall be used.
	 */
	void afb_req_fail_f(struct afb_req req, const char *status, const char *info, ...);

> For conveniency, these functions call **json_object_put** to release the object **obj**
> that they send. Then **obj** can not be used after calling one of these reply functions.
> When it is not the expected behaviour, calling the function **json_object_get** on the object **obj**
> before cancels the effect of **json_object_put**.

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
> ***json_object_get_string(json_object_object_get(afb_req_json(req), name))***

Initialisation of the plugin and declaration of verbs
-----------------------------------------------------

To be active, the verbs of the plugin should be declared to
afb-daemon. And even more, the plugin itself must be recorded.

The mechanism for doing this is very simple: when afb-need starts,
it loads the plugins that are listed in its argument or configuration.

Loading a plugin follows the following steps:

1. It loads the plugin using *dlopen*.

2. It searchs for the symbol named **pluginAfbV1Register** using *dlsym*.
This symbol is assumed to be the exported initialisation function of the plugin.

3. It build an interface object for the plugin.

4. It calls the found function **pluginAfbV1Register** and pass it the pointer
to its interface.

5. The function **pluginAfbV1Register** setup the plugin, initialize it.

6. The function **pluginAfbV1Register** returns the pointer to a structure
that describes the plugin: its version, its name (prefix or API name), and the
list of its verbs.

7. Afb-daemon checks that the returned version and name can be managed.
If it can manage it, the plugin and its verbs are recorded and can be used
when afb-daemon finishes it initialisation.

Here is the listing of the function **pluginAfbV1Register** of the plugin
*tic-tac-toe*:

	/*
	 * activation function for registering the plugin called by afb-daemon
	 */
	const struct AFB_plugin *pluginAfbV1Register(const struct AFB_interface *itf)
	{
	   afbitf = itf;         // records the interface for accessing afb-daemon
	   return &plugin_description;  // returns the description of the plugin
	}

This is a very small function because the *tic-tac-toe* plugin doesn't have initialisation step.
It merely record the daemon's interface and returns its descritption.

The variable **afbitf** is a variable global to the plugin. It records the
interface to afb-daemon and is used for logging and pushing events.
Here is its declaration:

	/*
	 * the interface to afb-daemon
	 */
	const struct AFB_interface *afbitf;

The description of the plugin is defined as below.

	/*
	 * array of the verbs exported to afb-daemon
	 */
	static const struct AFB_verb_desc_v1 plugin_verbs[] = {
	   /* VERB'S NAME     SESSION MANAGEMENT          FUNCTION TO CALL  SHORT DESCRIPTION */
	   { .name= "new",   .session= AFB_SESSION_NONE, .callback= new,   .info= "Starts a new game" },
	   { .name= "play",  .session= AFB_SESSION_NONE, .callback= play,  .info= "Asks the server to play" },
	   { .name= "move",  .session= AFB_SESSION_NONE, .callback= move,  .info= "Tells the client move" },
	   { .name= "board", .session= AFB_SESSION_NONE, .callback= board, .info= "Get the current board" },
	   { .name= "level", .session= AFB_SESSION_NONE, .callback= level, .info= "Set the server level" },
	   { .name= "join",  .session= AFB_SESSION_CHECK,.callback= join,  .info= "Join a board" },
	   { .name= "undo",  .session= AFB_SESSION_NONE, .callback= undo,  .info= "Undo the last move" },
	   { .name= "wait",  .session= AFB_SESSION_NONE, .callback= wait,  .info= "Wait for a change" },
	   { .name= NULL } /* marker for end of the array */
	};

	/*
	 * description of the plugin for afb-daemon
	 */
	static const struct AFB_plugin plugin_description =
	{
	   /* description conforms to VERSION 1 */
	   .type= AFB_PLUGIN_VERSION_1,
	   .v1= {				/* fills the v1 field of the union when AFB_PLUGIN_VERSION_1 */
	      .prefix= "tictactoe",		/* the API name (or plugin name or prefix) */
	      .info= "Sample tac-tac-toe game",	/* short description of of the plugin */
	      .verbs = plugin_verbs		/* the array describing the verbs of the API */
	   }
	};

The structure **plugin_description** describes the plugin.
It declares the type and version of the plugin, its name, a description
and a list of its verbs.

The list of verbs is an array of structures describing the verbs and terminated by a marker:
a verb whose name is NULL.

The description of the verbs for this version is made of 4 fields:

- the name of the verbs,

- the session management flags,

- the implementation function to be call for the verb,

- a short description.

The structure describing verbs is defined as follows:

	/*
	 * Description of one verb of the API provided by the plugin
	 * This enumeration is valid for plugins of type 1
	 */
	struct AFB_verb_desc_v1
	{
	       const char *name;                       /* name of the verb */
	       enum AFB_session_v1 session;            /* authorisation and session requirements of the verb */
	       void (*callback)(struct afb_req req);   /* callback function implementing the verb */
	       const char *info;                       /* textual description of the verb */
	};

For technical reasons, the enumeration **enum AFB_session_v1** is not exactly an
enumeration but the wrapper of constant definitions that can be mixed using bitwise or
(the C operator |).

The constants that can bit mixed are:

Constant name            | Meaning
-------------------------|-------------------------------------------------------------
**AFB_SESSION_CREATE**   | Equals to AFB_SESSION_LOA_EQ_0|AFB_SESSION_RENEW
**AFB_SESSION_CLOSE**    | Closes the session after the reply and set the LOA to 0
**AFB_SESSION_RENEW**    | Refreshes the token of authentification
**AFB_SESSION_CHECK**    | Just requires the token authentification
**AFB_SESSION_LOA_LE_0** | Requires the current LOA to be lesser then or equal to 0
**AFB_SESSION_LOA_LE_1** | Requires the current LOA to be lesser then or equal to 1
**AFB_SESSION_LOA_LE_2** | Requires the current LOA to be lesser then or equal to 2
**AFB_SESSION_LOA_LE_3** | Requires the current LOA to be lesser then or equal to 3
**AFB_SESSION_LOA_GE_0** | Requires the current LOA to be greater then or equal to 0
**AFB_SESSION_LOA_GE_1** | Requires the current LOA to be greater then or equal to 1
**AFB_SESSION_LOA_GE_2** | Requires the current LOA to be greater then or equal to 2
**AFB_SESSION_LOA_GE_3** | Requires the current LOA to be greater then or equal to 3
**AFB_SESSION_LOA_EQ_0** | Requires the current LOA to be equal to 0
**AFB_SESSION_LOA_EQ_1** | Requires the current LOA to be equal to 1
**AFB_SESSION_LOA_EQ_2** | Requires the current LOA to be equal to 2
**AFB_SESSION_LOA_EQ_3** | Requires the current LOA to be equal to 3

If any of this flags is set, afb-daemon requires the token authentification
as if the flag **AFB_SESSION_CHECK** had been set.

The special value **AFB_SESSION_NONE** is zero and can be used to avoid any check.

> Note that **AFB_SESSION_CREATE** and **AFB_SESSION_CLOSE** might be removed in later versions.

Sending messages to the log system
----------------------------------

Afb-daemon provides 4 levels of verbosity and 5 verbs for logging messages.

The verbosity is managed. Options allow the change the verbosity of afb-daemon
and the verbosity of the plugins can be set plugin by plugin.

The verbs for logging messages are defined as macros that test the
verbosity level and that call the real logging function only if the
message must be output. This avoid evaluation of arguments of the
formatting messages if the message must not be output.

### Verbs for logging messages

The 5 logging verbs are:

Macro   | Verbosity | Meaning                           | syslog level
--------|:---------:|-----------------------------------|:-----------:
ERROR   |     0     | Error conditions                  |     3
WARNING |     1     | Warning conditions                |     4
NOTICE  |     1     | Normal but significant condition  |     5
INFO    |     2     | Informational                     |     6
DEBUG   |     3     | Debug-level messages              |     7

You can note that the 2 verbs **WARNING** and **INFO** have the same level
of verbosity. But they don't have the same *syslog level*. It means that
they are output with a different level on the logging system.

All of these verbs have the same signature:

	void ERROR(const struct AFB_interface *afbitf, const char *message, ...);

The first argument **afbitf** is the interface to afb daemon that the
plugin received at its initialisation when **pluginAfbV1Register** was called.

The second argument **message** is a formatting string compatible with printf/sprintf.

The remaining arguments are arguments of the formating message like for printf.

### Managing verbosity

Depending on the level of verbosity, the messages are output or not.
The following table explains what messages will be output depending
ont the verbosity level.

Level of verbosity | Outputed macro
:-----------------:|--------------------------
        0          | ERROR
        1          | ERROR + WARNING + NOTICE
        2          | ERROR + WARNING + NOTICE + INFO
        3          | ERROR + WARNING + NOTICE + INFO + DEBUG

### Output format and destination

The syslog level is used for forging a prefix to the message.
The prefixes are:

syslog level | prefix
:-----------:|---------------
      0      | <0> EMERGENCY
      1      | <1> ALERT
      2      | <2> CRITICAL
      3      | <3> ERROR
      4      | <4> WARNING
      5      | <5> NOTICE
      6      | <6> INFO
      7      | <7> DEBUG


The message is issued to the standard error.
The final destination of the message depends on how the systemd service
was configured through the variable **StandardError**: It can be
journal, syslog or kmsg. (See man sd-daemon).

Sending events
--------------

Since version 0.5, plugins can broadcast events to any potential listener.
This kind of bradcast is not targeted. Event targeted will come in a future
version of afb-daemon.

The plugin *tic-tac-toe* broadcasts events when the board changes.
This is done in the function **changed**:

	/*
	 * signals a change of the board
	 */
	static void changed(struct board *board, const char *reason)
	{
		...
		struct json_object *description;

		/* get the description */
		description = describe(board);

		...

		afb_daemon_broadcast_event(afbitf->daemon, reason, description);
	}

The description of the changed board is pushed via the daemon interface.

Within the plugin *tic-tac-toe*, the *reason* indicates the origin of
the change. For the function **afb_daemon_broadcast_event**, the second
parameter is the name of the broadcasted event. The third argument is the
object that is transmitted with the event.

The function **afb_daemon_broadcast_event** is defined as below:

	/*
	 * Broadcasts widely the event of 'name' with the data 'object'.
	 * 'object' can be NULL.
	 * 'daemon' MUST be the daemon given in interface when activating the plugin.
	 *
	 * For conveniency, the function calls 'json_object_put' for 'object'.
	 * Thus, in the case where 'object' should remain available after
	 * the function returns, the function 'json_object_get' shall be used.
	 */
	void afb_daemon_broadcast_event(struct afb_daemon daemon, const char *name, struct json_object *object);

> Be aware, as for reply functions, the **object** is automatically released using
> **json_object_put** by the function. Then call **json_object_get** before
> calling **afb_daemon_broadcast_event** to keep **object** available
> after the returning of the function.

In fact the event name received by the listener is prefixed with
the name of the plugin. So when the change occurs after a move, the
reason is **move** and then the clients receive the event **tictactoe/move**.

> Note that nothing is said about the case sensitivity of event names.
> However, the event is always prefixed with the name that the plugin
> declared, with the same case, followed with a slash /.
> Thus it is safe to compare event using a case sensitive comparison.



Writing an asynchronous verb implementation
-------------------------------------------

The *tic-tac-toe* example allows two clients or more to share the same board.
This is implemented by the verb **join** that illustrated partly the how to
retrieve arguments.

When two or more clients are sharing a same board, one of them can wait
until the state of the board changes. (This coulded also be implemented using
events because an even is generated each time the board changes).

In this case, the reply to the wait is sent only when the board changes.
See the diagram below:

	CLIENT A       CLIENT B         TIC-TAC-TOE
	   |              |                  |
	   +--------------|----------------->| wait . . . . . . . .
	   |              |                  |                     .
	   :              :                  :                      .
	   :              :                  :                      .
	   |              |                  |                      .
	   |              +----------------->| move . . .           .
	   |              |                  |          V           .
	   |              |<-----------------+ success of move      .
	   |              |                  |                    .
	   |<-------------|------------------+ success of wait  <

Here, this is an invocation of the plugin by an other client that
unblock the suspended *wait* call.
But in general, this will be a timer, a hardware event, the sync with
a concurrent process or thread, ...

So the case is common, this is an asynchronous implementation.

Here is the listing of the function **wait**:

	static void wait(struct afb_req req)
	{
		struct board *board;
		struct waiter *waiter;

		/* retrieves the context for the session */
		board = board_of_req(req);
		INFO(afbitf, "method 'wait' called for boardid %d", board->id);

		/* creates the waiter and enqueues it */
		waiter = calloc(1, sizeof *waiter);
		waiter->req = req;
		waiter->next = board->waiters;
		afb_req_addref(req);
		board->waiters = waiter;
	}

After retrieving the board, the function adds a new waiter to the
current list of waiters and returns without sending a reply.

Before returning, it increases the reference count of the
request **req** using the function **afb_req_addref**.

> When the implentation of a verb returns without sending a reply,
> it **MUST** increment the reference count of the request
> using **afb_req_addref**. If it doesn't bad things can happen.

Later, when the board changes, it calls the function **changed**
of *tic-tac-toe* with the reason of the change.

Here is the full listing of the function **changed**:

	/*
	 * signals a change of the board
	 */
	static void changed(struct board *board, const char *reason)
	{
		struct waiter *waiter, *next;
		struct json_object *description;

		/* get the description */
		description = describe(board);

		waiter = board->waiters;
		board->waiters = NULL;
		while (waiter != NULL) {
			next = waiter->next;
			afb_req_success(waiter->req, json_object_get(description), reason);
			afb_req_unref(waiter->req);
			free(waiter);
			waiter = next;
		}

		afb_event_sender_push(afb_daemon_get_event_sender(afbitf->daemon), reason, description);
	}

The list of waiters is walked and a reply is sent to each waiter.
After the sending the reply, the reference count of the request
is decremented using **afb_req_unref** to allow its resources to be freed.

> The reference count **MUST** be decremented using **afb_req_unref** because,
> otherwise, there is a leak of resources.
> It must be decremented **AFTER** the sending of the reply, because, otherwise,
> bad things may happen.

How to build a plugin
---------------------

Afb-daemon provides a *pkg-config* configuration file that can be
queried by the name **afb-daemon**.
This configuration file provides data that should be used
for compiling plugins. Examples:

	$ pkg-config --cflags afb-daemon
	$ pkg-config --libs afb-daemon

### Example for cmake meta build system

This example is the extract for building the plugin *afm-main* using *CMAKE*.

	pkg_check_modules(afb afb-daemon)
	if(afb_FOUND)
		message(STATUS "Creation afm-main-plugin for AFB-DAEMON")
		add_library(afm-main-plugin MODULE afm-main-plugin.c)
		target_compile_options(afm-main-plugin PRIVATE ${afb_CFLAGS})
		target_include_directories(afm-main-plugin PRIVATE ${afb_INCLUDE_DIRS})
		target_link_libraries(afm-main-plugin utils ${afb_LIBRARIES})
		set_target_properties(afm-main-plugin PROPERTIES
			PREFIX ""
			LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/afm-main-plugin.export-map"
		)
		install(TARGETS afm-main-plugin LIBRARY DESTINATION ${plugin_dir})
	else()
		message(STATUS "Not creating the plugin for AFB-DAEMON")
	endif()

Let now describe some of these lines.

	pkg_check_modules(afb afb-daemon)

This first lines searches to the *pkg-config* configuration file for
**afb-daemon**. Resulting data are stored in the following variables:

Variable          | Meaning
------------------|------------------------------------------------
afb_FOUND         | Set to 1 if afb-daemon plugin development files exist
afb_LIBRARIES     | Only the libraries (w/o the '-l') for compiling afb-daemon plugins
afb_LIBRARY_DIRS  | The paths of the libraries (w/o the '-L') for compiling afb-daemon plugins
afb_LDFLAGS       | All required linker flags for compiling afb-daemon plugins
afb_INCLUDE_DIRS  | The '-I' preprocessor flags (w/o the '-I') for compiling afb-daemon plugins
afb_CFLAGS        | All required cflags for compiling afb-daemon plugins

If development files are found, the plugin can be added to the set of
target to build.

	add_library(afm-main-plugin MODULE afm-main-plugin.c)

This line asks to create a shared library having only the
source file afm-main-plugin.c (that is compiled).
The default name of the created shared object is
**libafm-main-plugin.so**.

	set_target_properties(afm-main-plugin PROPERTIES
		PREFIX ""
		LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/afm-main-plugin.export-map"
	)

This lines are doing two things:

1. It renames the built library from **libafm-main-plugin.so** to **afm-main-plugin.so**
by removing the implicitely added prefix *lib*. This step is not mandatory
at all because afb-daemon doesn't check names of files when loading it.
The only convention that use afb-daemon is that extension is **.so**
but this convention is used only when afb-daemon discovers plugin
from a directory hierarchy.

2. It applies a version script at link to only export the conventional name
of the entry point: **pluginAfbV1Register**. See below. By default, the linker
that creates the shared object exports all the public symbols (C functions that
are not **static**).

Next line are:

	target_include_directories(afm-main-plugin PRIVATE ${afb_INCLUDE_DIRS})
	target_link_libraries(afm-main-plugin utils ${afb_LIBRARIES})

As you can see it uses the variables computed by ***pkg_check_modules(afb afb-daemon)***
to configure the compiler and the linker.

### Exporting the function pluginAfbV1Register

The function **pluginAfbV1Register** must be exported. This can be achieved
using a version script when linking. Here is the version script that is
used for *tic-tac-toe* (plugins/samples/export.map).

	{ global: pluginAfbV1Register; local: *; };

This sample [version script](https://sourceware.org/binutils/docs-2.26/ld/VERSION.html#VERSION)
exports as global the symbol *pluginAfbV1Register* and hides any
other symbols.

This version script is added to the link options using the
option **--version-script=export.map** is given directly to the
linker or using th option **-Wl,--version-script=export.map**
when the option is given to the C compiler.

### Building within yocto

Adding a dependency to afb-daemon is enough. See below:

	DEPENDS += " afb-daemon "

