[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](LICENSE.md)
[![Build Status](https://travis-ci.org/kodi-pvr/pvr.wmc.svg?branch=Matrix)](https://travis-ci.org/kodi-pvr/pvr.wmc/branches)
[![Build Status](https://dev.azure.com/teamkodi/kodi-pvr/_apis/build/status/kodi-pvr.pvr.wmc?branchName=Matrix)](https://dev.azure.com/teamkodi/kodi-pvr/_build/latest?definitionId=67&branchName=Matrix)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/5120/badge.svg)](https://scan.coverity.com/projects/5120)

# Windows Media Center PVR
Windows Media Center PVR client addon for [Kodi] (https://kodi.tv)

## Build instructions

### Linux

1. `git clone --branch master https://github.com/xbmc/xbmc.git`
2. `git clone https://github.com/kodi-pvr/pvr.wmc.git`
3. `cd pvr.wmc && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.wmc -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

##### Useful links

* [Kodi's PVR user support] (https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support] (https://forum.kodi.tv/forumdisplay.php?fid=136)
