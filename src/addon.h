/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "settings.h"

#include <kodi/AddonBase.h>
#include <unordered_map>

class Pvr2Wmc;

class ATTR_DLL_LOCAL CPvr2WmcAddon : public kodi::addon::CAddonBase
{
public:
  CPvr2WmcAddon() = default;

  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override;
  void DestroyInstance(const kodi::addon::IInstanceInfo& instance,
                       const KODI_ADDON_INSTANCE_HDL hdl) override;

  ADDON_STATUS SetSetting(const std::string& settingName,
                          const kodi::addon::CSettingValue& settingValue) override;
  CSettings& GetSettings() { return _settings; }

private:
  CSettings _settings;
  std::unordered_map<std::string, Pvr2Wmc*> _usedInstances;
};
