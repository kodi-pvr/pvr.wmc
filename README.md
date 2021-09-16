[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build and run tests](https://github.com/kodi-pvr/pvr.wmc/actions/workflows/build.yml/badge.svg?branch=Matrix)](https://github.com/kodi-pvr/pvr.wmc/actions/workflows/build.yml)
[![Build Status](https://dev.azure.com/teamkodi/kodi-pvr/_apis/build/status/kodi-pvr.pvr.wmc?branchName=Matrix)](https://dev.azure.com/teamkodi/kodi-pvr/_build/latest?definitionId=67&branchName=Matrix)
[![Build Status](https://jenkins.kodi.tv/view/Addons/job/kodi-pvr/job/pvr.wmc/job/Matrix/badge/icon)](https://jenkins.kodi.tv/blue/organizations/jenkins/kodi-pvr%2Fpvr.wmc/branches/)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/5120/badge.svg)](https://scan.coverity.com/projects/5120)

# Windows Media Center PVR
Windows Media Center PVR client addon for [Kodi](https://kodi.tv)

## Build instructions

### Linux

1. `git clone --branch master https://github.com/xbmc/xbmc.git`
2. `git clone https://github.com/kodi-pvr/pvr.wmc.git`
3. `cd pvr.wmc && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=pvr.wmc -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

##### Useful links

* [Kodi's PVR user support](https://forum.kodi.tv/forumdisplay.php?fid=167)
* [Kodi's PVR development support](https://forum.kodi.tv/forumdisplay.php?fid=136)
