import QtQuick 2.0
import QtQuick.Window 2.0
import QtQuick.Controls 1.4
import QtWebSockets 1.0

Window {

	property string address_str: "ws://localhost:1234/api?token=123456"
	property string token_str: ""
	property string api_str: "auth"
	property string verb_str: ""
	property var msgid_enu: { "call":2, "retok":3, "reterr":4, "event":5 }
	property string request_str: ""
	property string status_str: ""

	visible: true
	width: 320
	height: 300

	WebSocket {
		id: websocket
		url: address_str
		onTextMessageReceived: {
			var message_json = JSON.parse (message);
			console.log ("Raw response: " + message)
			console.log ("JSON response: " + message_json)
			 /* server is not happy with our request, ignore it */
			if (message_json[0] != msgid_enu.retok) {
				console.log ("Return value is not ok !")
				return
			}
			 /* token creation or refresh happened, store it and enable buttons */
			if ((verb_str == "create") || (verb_str == "refresh")) {
				token_str = message_json[3]
				refresh_button.enabled = true
				reset_button.enabled = true
			 /* token reset happened, remove it and disable buttons */
			} else if (verb_str == "reset") {
				token_str = ""
				refresh_button.enabled = false
				reset_button.enabled = false
				websocket.active = false	// close the socket
			}
		}
		onStatusChanged: {
			if (websocket.status == WebSocket.Error) {
				status_str = "Error: " + websocket.errorString
			} else if (websocket.status == WebSocket.Open) {
		              	status_str = "Socket opened; sending message..."
				if (verb_str == "create")
					websocket.sendTextMessage (request_str)
				} else if (websocket.status == WebSocket.Closed) {
					status_str = "Socket closed"
		                }
				console.log (status_str)
		}
		active: false
	}

	Rectangle {
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.horizontalCenter: parent.horizontalCenter
		anchors.margins: 20

		Label {
			text: "QML Websocket AFB Sample"
			font.pixelSize: 18
			font.bold: true
			anchors.centerIn: parent
			y: 0
		}

		Text {
			id: url_notifier
			text: "URL: " + websocket.url
			y: 20
		}
		Text {
			id: verb_notifier
			text: "Verb: " + verb_str
			y: 40
		}
		Text {
			id: token_notifier
			text: "Token: " + token_str
			y: 60
		}

		Button {
			id: create_button
			text: "Create token"
			onClicked: {
				verb_str = "connect"
				request_str = '[' + msgid_enu.call + ',"99999","' + api_str+'/'+verb_str + '", ]';
				if (!websocket.active)
					websocket.active = true
				else
					websocket.sendTextMessage (request_str)
			}
			y: 80
		}
		Button {
			id: refresh_button
			text: "Refresh token"
			onClicked: {
				verb_str = "refresh"
				request_str = '[' + msgid_enu.call + ',"99999","' + api_str+'/'+verb_str + '",,"' + token_str +'" ]';
				websocket.sendTextMessage (request_str)
			}
			y: 110
			enabled: false
		}
		Button {
			id: reset_button
			text: "Reset token"
			onClicked: {
				verb_str = "logout"
				request_str = '[' + msgid_enu.call + ',"99999","' + api_str+'/'+verb_str + '", ]';
				websocket.sendTextMessage (request_str)
			}
			y: 140
			enabled: false
		} 

		Text {
			id: request_notifier
			text: "Request: " + request_str
			y: 170
		}

		Text {
			id: status_notifier
			text: "Status: " + status_str
			y: 190
		}

	}

}
