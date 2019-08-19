cmake_minimum_required(VERSION 3.5)
project(pvr.wmc)

find_package(Kodi REQUIRED)
find_package(p8-platform REQUIRED)

include_directories(${p8-platform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR})

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

set(DEPLIBS ${p8-platform_LIBRARIES})

if (WIN32)
  list(APPEND DEPLIBS ws2_32)
endif()

build_addon(pvr.wmc WMC DEPLIBS)

include(CPack)
