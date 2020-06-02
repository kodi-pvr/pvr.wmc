/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "clientversion.h"

#include "kodi/libXBMC_addon.h"
#include "kodi/libXBMC_pvr.h"

#include <p8-platform/util/StringUtils.h>

#define string_format StringUtils::Format
#define split StringUtils::Split

extern bool m_bCreated;
extern std::string g_strUserPath;
extern std::string g_strClientPath;
extern std::string g_strServerName; // the name of the server to connect to
extern std::string g_strClientName; // the name of the computer running addon
extern std::string g_clientOS; // OS of client, passed to server
extern int g_port;
extern bool g_bSignalEnable;
extern int g_signalThrottle;
extern bool g_bEnableMultiResume;
extern ADDON::CHelper_libXBMC_addon* XBMC;
extern CHelper_libXBMC_pvr* PVR;
extern std::string g_strServerMAC;
extern bool g_bWakeOnLAN;
extern std::string g_AddonDataCustom;

enum backend_status
{
  BACKEND_UNKNOWN,
  BACKEND_DOWN,
  BACKEND_UP
};
extern backend_status g_BackendOnline;
