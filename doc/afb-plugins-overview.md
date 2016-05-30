# Overview of plugins shipped with AFB-Daemon
    Version: 1
    Date:    30 May 2016
    Author:  Manuel Bachmann


## List of plugins
  Here are the plugins shipped in the source tree:
 * Hello World
 * Authentication
 * Tic Tac Toe
 * Audio _(2 backends: ALSA/PulseAudio)_
 * Radio _(1 backend: RTLSDR RTL2832U)_
 * Media _(1 backend: Rygel UPnP)_

All plugins may not be built, depending on the development libraries present on the system at build time.


## Detail of plugins


 ### Hello World

A sample Hello World plugin for demonstration and learning purposes.

This plugin provides a few unauthenticated requests, all beginning with "ping" ("_pingSample_", "_pingJson_", "_pingFail_"...) to demonstrate basic binder capabilities.


 ### Authentication

An sample Authentication plugin for demonstration purposes.

This plugin provides a few requests to demonstrate the binder's token-based security mechanism.

Calling "_connect_" with a security token will initiate a session, calling "_refresh_" will issue a new token and invalidate the previous one, calling "_logout_" will invalidate all tokens and close the session.


 ### Tic Tac Toe

A sample Tic Tac Toe game plugin.

This plugin provides an interactive Tic Tac Toe game where the binder returns the grid as a JSON response. 


 ### Audio

A sample Audio plugin with 2 backends:
 * ALSA (mandatory)
 * PulseAudio (optional)

This plugin is able to initialize a specific soundcard, define volume levels, channels (mono/stereo...), mute sound, and play a 22,050 Hz PCM stream.

_(if PulseAudio development libraries are not found at build time, only ALSA will be available)_

_(if a PulseAudio server is not found at runtime, the plugin will dynamically fall back to ALSA)_

_(a specifc backend can be forced by using this syntax before running afb-daemon : **$ export AFB_AUDIO_OUTPUT=Alsa**)_


 ### Radio

A sample AM/FM Radio plugin with 1 backend:
 * RTLSDR - Realtek RTL2832U dongles (mandatory)

This plugin is able to initialize specific RTL2832U dongles, switch between AM/FM modes, define frequency, mute sound, and play sound (if combining with the **audio** plugin).

_(if rtlsdr development libraries are not found at build time, this plugin will not be built)_


 ### Media

A sample Media Server plugin with 1 backend:
 * Rygel

_(if GUPnP/GSSDP development libraries are not fund at build time, this plugin will not be built)_

This plugin is able to detect a local Rygel UPnP media server, list audio files, select an audio file for playback, play/pause/seek in this file, upload an audio file to the server.


---
<br />

Sample command-line applications: _afb-client-demo_ (built by default)

Sample HTML5 applications: **test/*.html**, **[afb-client](https://github.com/iotbzh/afb-client)**, **[afb-radio](https://github.com/iotbzh/afb-radio)**

Sample Qt/QML applications: *test/token-websock.qml*
