(function (){'use strict';

  // some default values
  // note that how default values are defined/used may change
  var defaults = {
      token: '123456789',
      api: '/api'
    };

  var CALL = 2;
  var RETOK = 3;
  var RETERR = 4;
  var EVENT = 5;

  var PROTO1 = "x-afb-ws-json1";

  // Definition of the Angular module
  var AfbClientModule = angular.module('AfbClient', []);

  // The instanciation of the module
  AfbClientModule.factory('AfbClient', [ '$location', function ($location) {
    var refid = $location.host();
    var params = $location.search();
    return clients[refid] || (client[refid] = new AfbContext(refid, params));
  }]);

  // prototype for handling context by uuid and token
  function AfbContext(refid, params) {
    this.refid = refid;
    this.api = params.api || defaults.api;
    this.uhttp = params.uhttp || this.api+'/';
    this.uws = params.uws || this.api;
    this.token = params.token || defaults.token;
    this.uuid = params.uuid;
    this.ws = null;
  }

  // prototype of functions linked to AfbContext object
  AfbContext.prototype = {
    // call using websockets
    call: function(method, query) { return getws(this).call(method, query); },

    // call using get
    get: function(method, query) { return $http.get(this.uhttp+method, mixtu(this, query)); },

    // call using post
    post: function(method, query) { return $http.post(this.uhttp+method, mixtu(this, query)); }
  };

  // get the current websocket
  function getws(ctxt) {
    return ctxt.ws || (ctxt.ws = new AfbWebSocket(ctxt));
  }

  // inserts the current token in the answer
  function mixtu(ctxt, query) {
    return ("token" in query) ? query : angular.extend({token:ctxt.token},query);
  }

  // prototype for websocket
  function AfbWebSocket(ctxt) {
    var protos = [ PROTO1 ];
    this.context = ctxt;
    var url = "ws:" + ctxt.refid + ctxt.uws;
    var q = ctxt.uuid ? ("?x-afb-uuid=" + ctxt.uuid) : "";
    if (ctxt.token)
      q = (q ? (q + "&") : "?") + ("x-afb-token=" + ctxt.token);
    this.pendings = {};
    this.awaitens = {};
    this.counter = 0;
    this.ws = new WebSocket(url + q, protos);
    this.ws.onopen = onopen.bind(this);
    this.ws.onerror = onerror.bind(this);
    this.ws.onclose = onclose.bind(this);
    this.ws.onmessage = onmessage.bind(this);
    this.onopen = onopen;
    this.onabort = onabort;
  }

  AfbWebSocket.prototype = {
    call: function(method, query) {
        return new Promise((function(resolve, reject){
          var id = String(this.counter = 4095 & (this.counter + 1));
          while (id in this.pendings) id = String(this.counter = 4095 & (this.counter + 1));
          this.pendings[id] = [ resolve, reject ];
          var arr = [CALL, id, method, request ];
          var tok = this.context.token; if (tok) arr.push(tok);
          this.ws.send(angular.toJson(arr, 0));
        }).bind(this));
      },
    addEvent: function (name, handler) {
        (this.awaitens[name] || (this.awaitens[name] = [])).push(handler);
      },
    removeEvent: function (name, handler) {
        var a = this.awaitens[name];
        if (a) {
          var i = a.indexOf(handler);
          if (i >= 0) a.splice(i, 1);
        }
      }

  };

  function onmessage(ev) {
    var obj = angular.fromJson(ev.data);
    var id = obj[1];
    var ans = obj[2];
    if (obj[3])
      this.context.token = obj[3];
    switch (obj[0]) {
    case RETOK:  reply(this.pendings, id, ans, 0); break; 
    case RETERR: reply(this.pendings, id, ans, 1); break; 
    case EVENT:   fire(this.awaitens, id, ans);    break; 

    }
  }

  function fire(awaitens, name, data) {
    var a = awaitens[name];
    if (a) a.forEach(function(handler){handler(data);});
    var i = name.indexOf("/");
    if (i >= 0) {
      a = awaitens[name.substring(0,i)];
      if (a) a.forEach(function(handler){handler(data);});
    }
    a = awaitens["*"];
    if (a) a.forEach(function(handler){handler(data);});
  }

  function reply(pendings, id, ans, offset) {
    if (id in pendings) {
      var p = pendings[id];
      delete pendings[id];
      var f = p[offset];
      if (f) f(ans);
    }
  }




/*






	AFB_websocket = function(onopen, onabort) {
	}

	function onerror(event) {
		var f = this.onabort;
		if (f) {
			delete this.onopen;
			delete this.onabort;
			f && f(this);
		}
		this.onerror && this.onerror(this);
	}

	function onopen(event) {
		var f = this.onopen;
		delete this.onopen;
		delete this.onabort;
		f && f(this);
	}

	function onclose(event) {
		for (var id in this.pendings) {
			var ferr = this.pendings[id].onerror;
			ferr && ferr(null, this);
		}
		this.pendings = {};
		this.onclose && this.onclose();
	}

	function close() {
		this.ws.close();
	}

	function call(method, request) {
	}

*/

/*
  // Factory is a singleton and share its context within all instances.
  AfbClientModule.factory('AppCall', function ($http, AppConfig, $log) {
    var myCalls = {
      get : function(plugin, action, query, callback) {
        if (!query.token) query.token = AppConfig.session.token; // add token to provided query
        $http.get('/api/' + plugin + '/' + action , {params: query}).then (callback, callback);
      }
    };
    return myCalls;
  });
*/





})();
