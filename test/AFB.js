AFB = function(base, initialtoken){

var urlws = "ws://"+window.location.host+"/"+base;
var urlhttp = "http://"+window.location.host+"/"+base;

/*********************************************/
/****                                     ****/
/****             AFB_context             ****/
/****                                     ****/
/*********************************************/
var AFB_context;
{
	var UUID = undefined;
	var TOKEN = initialtoken;

	var context = function(token, uuid) {
		this.token = token;
		this.uuid = uuid;
	}

	context.prototype = {
		get token() {return TOKEN;},
		set token(tok) {if(tok) TOKEN=tok;},
		get uuid() {return UUID;},
		set uuid(id) {if(id) UUID=id;}
	};

	AFB_context = new context();
}
/*********************************************/
/****                                     ****/
/****             AFB_websocket           ****/
/****                                     ****/
/*********************************************/
var AFB_websocket;
{
	var CALL = 2;
	var RETOK = 3;
	var RETERR = 4;

	var PROTO1 = "x-afb-ws-json1";

	AFB_websocket = function(onopen, onabort) {
		this.ws = new WebSocket(urlws, [ PROTO1 ]);
		this.pendings = {};
		this.counter = 0;
		this.ws.onopen = onopen.bind(this);
		this.ws.onerror = onerror.bind(this);
		this.ws.onclose = onclose.bind(this);
		this.ws.onmessage = onmessage.bind(this);
		this.onopen = onopen;
		this.onabort = onabort;
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

	function onmessage(event) {
		var obj = JSON.parse(event.data);
		var code = obj[0];
		var id = obj[1];
		var ans = obj[2];
		AFB_context.token = obj[3];
		var pend;
		if (id && id in this.pendings) {
			pend = this.pendings[id];
			delete this.pendings[id];
		}
		switch (code) {
		case RETOK:
			pend && pend.onsuccess && pend.onsuccess(ans, this);
			break; 
		case RETERR:
		default:
			pend && pend.onerror && pend.onerror(ans, this);
			break; 
		}
	}

	function close() {
		this.ws.close();
	}

	function call(api, verb, request, onsuccess, onerror) {
		var id = String(++this.counter);
		this.pendings[id] = { onsuccess: onsuccess, onerror: onerror };
		var arr = [CALL, id, api+"/"+verb, request ];
		if (AFB_context.token) arr.push(AFB_context.token);
		this.ws.send(JSON.stringify(arr));
	}

	AFB_websocket.prototype = {
		close: close,
		call: call
	};
}
/*********************************************/
/****                                     ****/
/****                                     ****/
/****                                     ****/
/*********************************************/
return {
	context: AFB_context,
	ws: AFB_websocket
};
};

