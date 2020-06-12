/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "addon.h"

#include "pvr2wmc.h"

ADDON_STATUS CPvr2WmcAddon::CreateInstance(int instanceType,
                                           const std::string& instanceID,
                                           KODI_HANDLE instance,
                                           const std::string& version,
                                           KODI_HANDLE& addonInstance)
{
  ADDON_STATUS status = ADDON_STATUS_UNKNOWN;

  if (instanceType == ADDON_INSTANCE_PVR)
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s - Creating the PVR-WMC add-on instance", __func__);

    _settings.Load();

    Pvr2Wmc* client = new Pvr2Wmc(*this, instance, version); // create interface to ServerWMC
    if (client->IsServerDown()) // check if server is down, if it is shut her down
    {
      status = ADDON_STATUS_LOST_CONNECTION;
    }
    else
    {
      status = ADDON_STATUS_OK;
    }

    addonInstance = client;
    _usedInstances.emplace(std::make_pair(instanceID, client));
  }

  return status;
}

void CPvr2WmcAddon::DestroyInstance(int instanceType,
                                    const std::string& instanceID,
                                    KODI_HANDLE addonInstance)
{
  if (instanceType == ADDON_INSTANCE_PVR)
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s - Destoying the PVR-WMC add-on instance", __func__);

    const auto& it = _usedInstances.find(instanceID);
    if (it != _usedInstances.end())
    {
      it->second->UnLoading();
      _usedInstances.erase(it);
    }
  }
}

ADDON_STATUS CPvr2WmcAddon::SetSetting(const std::string& settingName,
                                       const kodi::CSettingValue& settingValue)
{
  return _settings.SetSetting(settingName, settingValue);
}

ADDONCREATOR(CPvr2WmcAddon)
