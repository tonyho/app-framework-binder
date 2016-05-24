HOWTO WRITE a PLUGIN for AFB-DAEMON
===================================
    version: 1
    Date:    24 mai 2016
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
verb implementation.

### The incoming request

For any implementation,  the request is received by a structure of type
**struct afb_req**.

***Important: note that this is a PLAIN structure, not a pointer to a structure.***

This structure, here named *req*, is used

*req* is used to get arguments of the request, to send
answer, to store session data.

This object and its interface is defined and documented
in the file names *afb/afb-req-itf.h*

The above example uses 2 times the request object *req*.

The first time, it is used for retrieving the board attached to
the session of the request.

The second time, it is used to send the reply: an object that
describes the current board.

### Associating an object to the session for the plugin

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

This function is very simple because it merely wraps
a call to the function **afb_req_context**, providing
all needed arguments.
The casts are required to avoid a warning when compiling.

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

This powerful function ensures that the context exists and is
stored for the session.

The function **get_new_board** creates a new board and set its
count of use to 1. The boards are counting their count of use
to free there ressources when no more used.

The function **release_board** 

### Sending the reply to a request

Getting argument of invocation
------------------------------

How to build a plugin
---------------------

Afb-daemon provides a The packaging of afb-daemon


