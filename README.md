### Application Framework Binder
This is an undergoing work, publication is only intended for developers to review and provide feedback.

### Licence
As today the code is under GLPV3, while no decision as been taken yet, it will certainly move under a different licence like GPV2, Apache or MIT.

Final goal is keep the engine public while allowing people to load non open-source plugins. The code already leverage other opensource 
libraries especially libmicrohttpd & libjson. Finally what ever Licence is chosen it should be compatible with dependencies and automotive
industry requirementsas the primary target for this code is AGL. 

### Start
afb-daemon --help 

### Example
afb-daemon --verbose --rootdir=/home/fulup/.AFB --alias=icons:/usr/share/icons

### Directory & Path
Default behaviour is to locate ROOTDIR in $HOME/.AFB

### REST API

Developer should mainly provides a structure with APIs name, corresponding methods and optionally some context and a handle.
A handle is a void* structure that it is passed to the api callback. The API receive the query and well as post data, incase
a post method was used. Every method should return a JSON object or NULL in case of error.

API plugin can be protected from timeout and other errors. By default this behaviour is deactivated, use --apitimeout to activate it.
        
        STATIC  AFB_restapi myApis[]= {
          {"ping"     , (AFB_apiCB)ping    , "Ping Function", NULL},
          {"action1"  , (AFB_apiCB)action1 , "Action-1", OptionalHandle},
          {"action2"  , (AFB_apiCB)action2 , "Action-2", OptionalHandle},
          {0,0,0}
        };

        PUBLIC AFB_plugin *pluginRegister (AFB_session *session) {
            AFB_plugin *plugin = malloc (sizeof (AFB_plugin));
            plugin->type  = AFB_PLUGIN;
            plugin->info  = "Plugin Sample";
            plugin->prefix= "myplugin";        
            plugin->apis  = myApis;
            return (plugin);
        }

### HTML5 and Angular Redirect

Binder support HTML5 redirect mode even with an application baseurl. Default value for application base URL is /opa
See Application Framework HTML5 Client template at https://github.com/iotbzh/afb-client-sample

If the Binder receive something like http://myopa/sample when sample is not the homepage of the Angular OPA. The the serveur
will redirect to http://myopa/#!sample this redirect will return the Index.html OPA file and will notify angular not to display
the homepage by to goto samplepage.

Warning: in order Angular application to work both with a BASEURL="/" and BASEURL="/MyApp/" every page references have to be relative.
Recommended model is to develop with a BASEURL="/opa" as any application working with a BASEURL will work without, when the opposite it not true.

Note: If a resource is not accessible from ROOTDIR then --alias should be use ex: --alias=/icons:/usr/share/icons. Only alias designed to access
external support static files. They should not be used for API and OPA.


### On going work

 -- Dynamic load of plugins. While everything is designed to support dynamically loadable plugins, this part is not done yet.
 -- Javascript plugins. As today only C plugins are supported, but JS plugins are on the ToDo list.

