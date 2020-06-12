/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/AddonBase.h>

#define LOCALHOST "127.0.0.1"

#define DEFAULT_PORT 9080
#define DEFAULT_WAKEONLAN_ENABLE false
#define DEFAULT_SIGNAL_ENABLE false
#define DEFAULT_SIGNAL_THROTTLE 10
#define DEFAULT_MULTI_RESUME true

class ATTRIBUTE_HIDDEN CSettings
{
public:
  CSettings() = default;

  bool Load();
  ADDON_STATUS SetSetting(const std::string& settingName, const kodi::CSettingValue& settingValue);

  const std::string& GetServerName() const { return _strServerName; }
  const std::string& GetClientName() const { return _strClientName; }
  const std::string& GetServerMAC() const { return _strServerMAC; }
  void SetServerMAC(const std::string& strServerMAC) { _strServerMAC = strServerMAC; }
  const std::string& GetClientOS() const { return _clientOS; }
  bool GetWakeOnLAN() const { return _bWakeOnLAN; }
  int GetPort() const { return _port; }
  bool GetSignalEnable() const { return _bSignalEnable; }
  int GetSignalThrottle() const { return _signalThrottle; }
  bool GetEnableMultiResume() const { return _bEnableMultiResume; }
  const std::string& GetAddonDataCustom() const { return _strAddonDataCustom; }

private:
  std::string _strServerName = LOCALHOST; // the name of the server to connect to
  std::string _strClientName; // the name of the computer running addon
  std::string _strServerMAC; // MAC address of server
  std::string _clientOS; // OS of client, passed to server
  bool _bWakeOnLAN = false; // whether to send wake on LAN to server
  int _port = DEFAULT_PORT;
  bool _bSignalEnable = DEFAULT_SIGNAL_ENABLE;
  int _signalThrottle = DEFAULT_SIGNAL_THROTTLE;
  bool _bEnableMultiResume = DEFAULT_MULTI_RESUME;
  std::string _strAddonDataCustom; // location of custom addondata settings file
};
