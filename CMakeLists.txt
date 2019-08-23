cmake_minimum_required(VERSION 3.5)
project(pvr.wmc)

find_package(Kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(p8-platform REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${p8-platform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}/..) # Hack way with "/..", need bigger Kodi cmake rework to match right include ways

add_definitions(-DUSE_DEMUX)

set(WMC_SOURCES src/client.cpp
                src/pvr2wmc.cpp
                src/Socket.cpp
                src/utilities.cpp)

set(WMC_HEADERS src/client.h
                src/clientversion.h
                src/pvr2wmc.h
                src/Socket.h
                src/utilities.h)

set(DEPLIBS ${kodiplatform_LIBRARIES}
            ${p8-platform_LIBRARIES})

if (WIN32)
  list(APPEND DEPLIBS ws2_32)
endif()

build_addon(pvr.wmc WMC DEPLIBS)

include(CPack)
