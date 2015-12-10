SET(AFB-DAEMON_HEADERS local-def.h proto-def.h)
SET(AFB-DAEMON_SOURCES main.c config.c session.c http-svc.c afbs-api.c dbus-api.c rest-api.c alsa-api.c)

IF(librtlsdr_FOUND)
  ADD_DEFINITIONS(-DWITH_RADIO_PLUGIN=1)
  SET(AFB-DAEMON_SOURCES ${AFB-DAEMON_SOURCES} radio-api.c)
ENDIF(librtlsdr_FOUND)

ADD_EXECUTABLE(afb-daemon ${AFB-DAEMON_SOURCES})

include_directories(${include_dirs})
target_link_libraries(afb-daemon ${link_libraries})

INSTALL(TARGETS afb-daemon
        RUNTIME DESTINATION bin)
