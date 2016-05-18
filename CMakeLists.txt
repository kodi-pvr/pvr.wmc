project(pvr.wmc)

cmake_minimum_required(VERSION 2.6)

enable_language(CXX)

find_package(Kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(p8-platform REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${p8-platform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR})

add_definitions(-DUSE_DEMUX)

set(WMC_SOURCES src/client.cpp
                src/pvr2wmc.cpp
                src/Socket.cpp
                src/utilities.cpp)

set(DEPLIBS ${kodiplatform_LIBRARIES}
            ${p8-platform_LIBRARIES})

if (WIN32)
  list(APPEND DEPLIBS ws2_32)
endif()

build_addon(pvr.wmc WMC DEPLIBS)

include(CPack)
