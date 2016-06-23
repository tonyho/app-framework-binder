INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(alsa alsa)
PKG_CHECK_MODULES(pulseaudio libpulse libpulse-simple)
INCLUDE(FindThreads)
FIND_PACKAGE(Threads)

IF(alsa_FOUND)

  MESSAGE(STATUS "ALSA found ; will compile Audio binding... (BINDING)")

  IF(pulseaudio_FOUND)
    MESSAGE(STATUS "PulseAudio found ; Audio binding will have PulseAudio support")
    ADD_DEFINITIONS(-DHAVE_PULSE=1)
    SET(pulse_sources audio-pulse.c)
  ENDIF(pulseaudio_FOUND)

  INCLUDE_DIRECTORIES(${include_dirs} ${alsa_INCLUDE_DIRS} ${pulseaudio_INCLUDE_DIRS})
  ADD_LIBRARY(audio-api MODULE audio-api.c audio-alsa.c ${pulse_sources})
  SET_TARGET_PROPERTIES(audio-api PROPERTIES
	PREFIX ""
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map"
  )
  TARGET_LINK_LIBRARIES(audio-api ${link_libraries} ${alsa_LIBRARIES} ${pulseaudio_LIBRARIES})
  INSTALL(TARGETS audio-api
          LIBRARY DESTINATION ${binding_install_dir})

ENDIF(alsa_FOUND)
