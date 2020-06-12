/*
 *  Copyright (C) 2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "settings.h"

#include "utilities.h"

#include <kodi/Network.h>

bool CSettings::Load()
{
  /* Read setting "port" from settings.xml */
  if (!kodi::CheckSettingInt("port", _port))
  {
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'port' setting, using '%i'", DEFAULT_PORT);
  }

  if (kodi::CheckSettingString("host", _strServerName))
  {
    kodi::Log(ADDON_LOG_DEBUG, "Settings: host='%s', port=%i", _strServerName.c_str(), _port);
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'host' setting, using '127.0.0.1'");
  }

  if (!kodi::CheckSettingBoolean("wake_on_lan", _bWakeOnLAN))
  {
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'wake_on_lan' setting, using '%s'",
              DEFAULT_WAKEONLAN_ENABLE);
  }

  std::string fileContent;
  if (Utils::ReadFileContents(_strAddonDataCustom, fileContent))
  {
    _strServerMAC = fileContent;
    kodi::Log(ADDON_LOG_ERROR, "Using ServerWMC MAC address from custom addondata '%s'",
              _strServerMAC.c_str());
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR,
              "Couldn't get ServerWMC MAC address from custom addondata, using empty value");
  }

  if (!kodi::CheckSettingBoolean("signal", _bSignalEnable))
  {
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'signal' setting, using '%s'", DEFAULT_SIGNAL_ENABLE);
  }

  if (!kodi::CheckSettingInt("signal_throttle", _signalThrottle))
  {
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'signal_throttle' setting, using '%s'",
              DEFAULT_SIGNAL_THROTTLE);
  }

  if (!kodi::CheckSettingBoolean("multiResume", _bEnableMultiResume))
  {
    kodi::Log(ADDON_LOG_ERROR, "Couldn't get 'multiResume' setting, using '%s'",
              DEFAULT_MULTI_RESUME);
  }


  // get the name of the computer client is running on
  _strClientName = kodi::network::GetHostname();

#ifdef TARGET_WINDOWS
  _clientOS = "windows"; // set to the client OS name
#elif defined TARGET_LINUX
  _clientOS = "linux"; // set to the client OS name
#elif defined TARGET_DARWIN
  _clientOS = "darwin"; // set to the client OS name
#elif defined TARGET_FREEBSD
  _clientOS = "freeBSD"; // set to the client OS name
#else
  _clientOS = ""; // set blank client OS name
#endif

  return true;
}

ADDON_STATUS CSettings::SetSetting(const std::string& settingName,
                                   const kodi::CSettingValue& settingValue)
{
  std::string sName = settingName;

  if (sName == "host")
  {
    std::string oldName = _strServerName;
    _strServerName = settingValue.GetString();

    kodi::Log(ADDON_LOG_INFO, "Setting 'host' changed from %s to %s", oldName.c_str(),
              _strServerName.c_str());
    if (oldName != _strServerName)
      return ADDON_STATUS_NEED_RESTART;
  }

  return ADDON_STATUS_OK;
}
