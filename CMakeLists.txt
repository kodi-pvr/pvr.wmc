cmake_minimum_required(VERSION 3.5)
project(pvr.wmc)

find_package(Kodi REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/..) # Hack way with "/..", need bigger Kodi cmake rework to match right include ways

set(WMC_SOURCES src/addon.cpp
                src/pvr2wmc.cpp
                src/settings.cpp
                src/Socket.cpp
                src/utilities.cpp)

set(WMC_HEADERS src/addon.h
                src/pvr2wmc.h
                src/settings.h
                src/Socket.h
                src/utilities.h)

if (WIN32)
  list(APPEND DEPLIBS ws2_32)
  add_compile_options(/wd4996) # Need gethostbyname and inet_addr change to prevent
endif()

# Set values for addon.xml.in
if(NOT OS)
  if(CORE_SYSTEM_NAME STREQUAL linux)
    set(OS "Linux")
  elseif(CORE_SYSTEM_NAME STREQUAL android)
    set(OS "Android")
  elseif(CORE_SYSTEM_NAME STREQUAL darwin_embedded)
    set(OS "Darwin embedded")
  elseif(CORE_SYSTEM_NAME STREQUAL osx)
    set(OS "OSX")
  elseif(CORE_SYSTEM_NAME STREQUAL windows)
    set(OS "Windows")
  else()
    set(OS "Unknown")
  endif()
endif()
if(NOT ARCHITECTURE)
  set(ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif()

build_addon(pvr.wmc WMC DEPLIBS)

add_definitions(-DWMC_VERSION=${WMC_VERSION})

include(CPack)
