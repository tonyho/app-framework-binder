# Overview of plugins shipped with AFB-Daemon
    Version: 1
    Date:    30 May 2016
    Author:  Manuel Bachmann

## List of plugins
  Here are the plugins shipped in the source tree:
 * Audio _(2 backends: ALSA/PulseAudio)_
 * Radio _(1 backend: RTLSDR RTL2832U)_
 * Media _(1 backend: Rygel UPnP)_

All plugins may not be built, depending on the development libraries present on the system at build time.

## Detail of plugins

 ### Audio

A sample Audio plugin with 2 backends:
 * ALSA (mandatory)
 * PulseAudio (optional)

_(if PulseAudio development libraries are not found at build time, only ALSA will be available)_

_(if a PulseAudio server is not found at runtime, the plugin will dynamically fall back to ALSA)_

_(a specifc backend can be forced by using this syntax before running afb-daemon : **$ export AFB_AUDIO_OUTPUT=Alsa**)_

This plugin is able to initialize a specific soundcard, define volume levels, channels (mono/stereo...), mute sound, and play a 22,050 Hz PCM stream.

Sample applications: **[afb-radio](https://github.com/iotbzh/afb-radio)**

 ### Radio

A sample AM/FM Radio plugin with 1 backend:
 * RTLSDR - Realtek RTL2832U dongles (mandatory)

_(if rtlsdr development libraries are not found at build time, this plugin will not be built)_

This plugin is able to initialize specific RTL2832U dongles, switch between AM/FM modes, define frequency, mute sound, and play sound (by using the **audio** plugin).

Sample applications: **[afb-radio](https://github.com/iotbzh/afb-radio)**

 ### Media

A sample Media Server plugin with 1 backend:
 * Rygel

_(if GUPnP/GSSDP development libraries are not fund at build time, this plugin will not be built)_

This plugin is able to detect a local Rygel UPnP media server, list audio files, select an audio file for playback, play/pause/seek in this file, upload an audio file to the server.

Sample applications: **[afb-radio](https://github.com/iotbzh/afb-radio)**

