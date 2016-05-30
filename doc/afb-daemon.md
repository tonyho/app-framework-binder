The binder AFB-DAEMON
=====================
    version: 1
    Date:    30 mai 2016
    Author:  José Bollo

TABLE-OF-CONTENT-HERE

Launching the binder afb-daemon
-------------------------------

The launch options for binder **afb-daemon** are:

	  --help

		Prints help with available options

	  --version

		Display version and copyright

	  --verbose

		Increases the verbosity, can be repeated

	  --port=xxxx

		HTTP listening TCP port  [default 1234]

	  --rootdir=xxxx

		HTTP Root Directory [default $AFBDIR or else $HOME/.AFB]

	  --rootbase=xxxx

		Angular Base Root URL [default /opa]

		This is used for any application of kind OPA (one page application).
		When set, any missing document whose url has the form /opa/zzz
		is translated to /opa/#!zzz

	  --rootapi=xxxx

		HTML Root API URL [default /api]

		The plugins are available within that url.

	  --alias=xxxx

		Maps a path located anywhere in the file system to the
		a subdirectory. The syntax for mapping a PATH to the
		subdirectory NAME is: --alias=/NAME:PATH.

		Example: --alias=/icons:/usr/share/icons maps the
		content of /usr/share/icons within the subpath /icons.

		This option can be repeated.

	  --apitimeout=xxxx

		Plugin API timeout in seconds [default 20]

		Defines how many seconds maximum a method is allowed to run.
		0 means no limit.

	  --cntxtimeout=xxxx

		Client Session Timeout in seconds [default 3600]

	  --cache-eol=xxxx

		Client cache end of live [default 100000 that is 27,7 hours]

	  --sessiondir=xxxx

		Sessions file path [default rootdir/sessions]

	  --ldpaths=xxxx

		Load Plugins from given paths separated by colons
		as for dir1:dir2:plugin1.so:... [default = $libdir/afb]

		You can mix path to directories and to plugins.
		The sub-directories of the given directories are searched
		recursively.

		The plugins are the files terminated by '.so' (the extension
		so denotes shared object) that contain the public entry symbol.

	  --plugin=xxxx

		Load the plugin of given path.

	  --token=xxxx

		Initial Secret token to authenticate.

		If not set, no client can authenticate.

		If set to the empty string, then any initial token is accepted.

	  --mode=xxxx

		Set the mode: either local, remote or global.

		The mode indicate if the application is run locally on the host
		or remotely through network.

	  --readyfd=xxxx

		Set the #fd to signal when ready

		If set, the binder afb-daemon will write "READY=1\n" on the file
		descriptor whose number if given (/proc/self/fd/xxx).

	  --dbus-client=xxxx

		Transparent binding to a binder afb-daemon service through dbus.

		It creates an API of name xxxx that is implemented remotely
		and queried via DBUS.

	  --dbus-server=xxxx

		Provides a binder afb-daemon service through dbus.

		The name xxxx must be the name of an API defined by a plugin.
		This API is exported through DBUS.

	  --foreground

		Get all in foreground mode (default)

	  --daemon

		Get all in background mode


Working with afb-daemon
-----------------------



Future of afb-daemon
--------------------

- Integration of the protocol JSON-RPC for the websockets.

- The binder afb-daemon would launch the applications directly.

- The current setting of mode (local/remote/global) might be reworked to a
mechanism for querying configuration variables.

- Implements "one-shot" initial token. It means that after its first
authenticated use, the initial token is removed and no client can connect
anymore.


