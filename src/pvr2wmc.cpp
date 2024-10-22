/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "pvr2wmc.h"

#include "utilities.h"

#include <kodi/General.h>
#include <thread>

long Pvr2Wmc::_buffTimesCnt; // filter how often we do buffer status reads
long Pvr2Wmc::_buffTimeFILTER;

Pvr2Wmc::Pvr2Wmc(CPvr2WmcAddon& addon, const kodi::addon::IInstanceInfo& instance)
  : kodi::addon::CInstancePVRClient(instance), _socketClient(*this), _addon(addon)
{
  _socketClient.SetServerName(_addon.GetSettings().GetServerName());
  _socketClient.SetClientName(_addon.GetSettings().GetClientName());
  _socketClient.SetServerPort(_addon.GetSettings().GetPort());
}

bool Pvr2Wmc::IsServerDown()
{
  std::string request;
  request = Utils::Format("GetServiceStatus|%s|%s", STR(WMC_VERSION),
                          _addon.GetSettings().GetClientOS().c_str());
  _socketClient.SetTimeOut(10); // set a timout interval for checking if server is down
  std::vector<std::string> results = _socketClient.GetVector(request, true); // get serverstatus
  bool isServerDown = (results[0] != "True"); // true if server is down

  // GetServiceStatus may return any updates requested by server
  if (!isServerDown && results.size() > 1) // if server is not down and there are additional fields
  {
    ExtractDriveSpace(results); // get drive space total/used from backend response
    TriggerUpdates(results); // send update array to trigger updates requested by server
  }
  return isServerDown;
}

void Pvr2Wmc::UnLoading()
{
  _socketClient.GetBool("ClientGoingDown", true, false); // returns true if server is up
}

PVR_ERROR Pvr2Wmc::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(true);
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(true);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetHandlesInputStream(true);
  capabilities.SetHandlesDemuxing(false);
  capabilities.SetSupportsRecordingPlayCount(true);
  capabilities.SetSupportsLastPlayedPosition(_addon.GetSettings().GetEnableMultiResume());
  capabilities.SetSupportsRecordingEdl(true);
  capabilities.SetSupportsRecordingsRename(true);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetBackendName(std::string& name)
{
  name = "ServerWMC";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetBackendVersion(std::string& version)
{
  if (!IsServerDown())
  {
    // Send client's time (in UTC) to backend
    time_t now = time(nullptr);
    char datestr[32];
    strftime(datestr, 32, "%Y-%m-%d %H:%M:%S", gmtime(&now));

    // Also send this client's setting for backend servername (so server knows how it is being
    // accessed)
    std::string request;
    request = Utils::Format("GetServerVersion|%s|%s", datestr,
                            _addon.GetSettings().GetServerName().c_str());
    std::vector<std::string> results = _socketClient.GetVector(request, true);
    if (results.size() > 0)
    {
      version = std::string(results[0]);
    }
    if (results.size() > 1)
    {
      _serverBuild = atoi(results[1].c_str()); // get server build number for feature checking
    }
    // check if recorded tv folder is accessible from client
    if (results.size() > 2 && results[2] != "") // if server sends empty string, skip check
    {
      std::vector<kodi::vfs::CDirEntry> items;
      if (!kodi::vfs::DirectoryExists(results[2]))
      {
        kodi::Log(ADDON_LOG_ERROR, "Recorded tv '%s' does not exist", results[2].c_str());
        std::string infoStr = kodi::addon::GetLocalizedString(30017);
        kodi::QueueNotification(QUEUE_ERROR, "", infoStr);
      }
      else if (!kodi::vfs::GetDirectory(results[2], "", items))
      {
        kodi::Log(ADDON_LOG_ERROR, "Recorded tv '%s' count not be opened", results[2].c_str());
        std::string infoStr = kodi::addon::GetLocalizedString(30018);
        kodi::QueueNotification(QUEUE_ERROR, "", infoStr);
      }
    }
    // check if server returned it's MAC address
    if (results.size() > 3 && results[3] != "" && results[3] != _addon.GetSettings().GetServerMAC())
    {
      kodi::Log(ADDON_LOG_INFO, "Setting ServerWMC Server MAC Address to '%s'", results[3].c_str());
      _addon.GetSettings().SetServerMAC(results[3]);

      // Attempt to save MAC address to custom addon data
      Utils::WriteFileContents(kodi::addon::GetUserPath("ServerMACAddr.txt"),
                               _addon.GetSettings().GetServerMAC());
    }

    return PVR_ERROR_NO_ERROR;
  }

  version = "Not accessible"; // server version check failed
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR Pvr2Wmc::GetBackendHostname(std::string& hostname)
{
  hostname = _addon.GetSettings().GetServerName();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetConnectionString(std::string& connection)
{
  connection = Utils::Format("%s:%u", _addon.GetSettings().GetServerName().c_str(),
                             _addon.GetSettings().GetPort());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  total = _diskTotal;
  used = _diskUsed;

  return PVR_ERROR_NO_ERROR;
}

namespace
{
struct TimerType : kodi::addon::PVRTimerType
{
  TimerType(unsigned int id,
            unsigned int attributes,
            const std::string& description,
            const std::vector<kodi::addon::PVRTypeIntValue>& priorityValues,
            int priorityDefault,
            const std::vector<kodi::addon::PVRTypeIntValue>& lifetimeValues,
            int lifetimeDefault,
            const std::vector<kodi::addon::PVRTypeIntValue>& maxRecordingsValues,
            int maxRecordingsDefault,
            const std::vector<kodi::addon::PVRTypeIntValue>& dupEpisodesValues,
            int dupEpisodesDefault)
  {
    SetId(id);
    SetAttributes(attributes);
    SetPriorities(priorityValues, priorityDefault);
    SetLifetimes(lifetimeValues, lifetimeDefault);
    SetMaxRecordings(maxRecordingsValues, maxRecordingsDefault);
    SetPreventDuplicateEpisodes(dupEpisodesValues, dupEpisodesDefault);
    SetDescription(description);
  }
};

} // unnamed namespace

PVR_ERROR Pvr2Wmc::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  /* PVR_Timer.iPriority values and presentation.*/
  static std::vector<kodi::addon::PVRTypeIntValue> priorityValues;
  if (priorityValues.size() == 0)
  {
    priorityValues.emplace_back(WMC_PRIORITY_NORMAL, kodi::addon::GetLocalizedString(30140));
    priorityValues.emplace_back(WMC_PRIORITY_HIGH, kodi::addon::GetLocalizedString(30141));
    priorityValues.emplace_back(WMC_PRIORITY_LOW, kodi::addon::GetLocalizedString(30142));
  }

  /* PVR_Timer.iLifeTime values and presentation.*/
  static std::vector<kodi::addon::PVRTypeIntValue> lifetimeValues;
  if (lifetimeValues.size() == 0)
  {
    lifetimeValues.emplace_back(WMC_LIFETIME_NOTSET, kodi::addon::GetLocalizedString(30160));
    lifetimeValues.emplace_back(WMC_LIFETIME_LATEST, kodi::addon::GetLocalizedString(30161));
    lifetimeValues.emplace_back(WMC_LIFETIME_WATCHED, kodi::addon::GetLocalizedString(30162));
    lifetimeValues.emplace_back(WMC_LIFETIME_ELIGIBLE, kodi::addon::GetLocalizedString(30163));
    lifetimeValues.emplace_back(WMC_LIFETIME_DELETED, kodi::addon::GetLocalizedString(30164));
    lifetimeValues.emplace_back(WMC_LIFETIME_ONEWEEK, kodi::addon::GetLocalizedString(30165));
  }

  /* PVR_Timer.iMaxRecordings values and presentation. */
  static std::vector<kodi::addon::PVRTypeIntValue> recordingLimitValues;
  if (recordingLimitValues.size() == 0)
  {
    recordingLimitValues.emplace_back(WMC_LIMIT_ASMANY, kodi::addon::GetLocalizedString(30170));
    recordingLimitValues.emplace_back(WMC_LIMIT_1, kodi::addon::GetLocalizedString(30171));
    recordingLimitValues.emplace_back(WMC_LIMIT_2, kodi::addon::GetLocalizedString(30172));
    recordingLimitValues.emplace_back(WMC_LIMIT_3, kodi::addon::GetLocalizedString(30173));
    recordingLimitValues.emplace_back(WMC_LIMIT_4, kodi::addon::GetLocalizedString(30174));
    recordingLimitValues.emplace_back(WMC_LIMIT_5, kodi::addon::GetLocalizedString(30175));
    recordingLimitValues.emplace_back(WMC_LIMIT_6, kodi::addon::GetLocalizedString(30176));
    recordingLimitValues.emplace_back(WMC_LIMIT_7, kodi::addon::GetLocalizedString(30177));
    recordingLimitValues.emplace_back(WMC_LIMIT_10, kodi::addon::GetLocalizedString(30178));
  }

  /* PVR_Timer.iPreventDuplicateEpisodes values and presentation.*/
  static std::vector<kodi::addon::PVRTypeIntValue> showTypeValues;
  if (showTypeValues.size() == 0)
  {
    showTypeValues.emplace_back(WMC_SHOWTYPE_FIRSTRUNONLY, kodi::addon::GetLocalizedString(30150));
    showTypeValues.emplace_back(WMC_SHOWTYPE_ANY, kodi::addon::GetLocalizedString(30151));
    showTypeValues.emplace_back(WMC_SHOWTYPE_LIVEONLY, kodi::addon::GetLocalizedString(30152));
  }

  static std::vector<kodi::addon::PVRTypeIntValue> emptyList;

  static const unsigned int TIMER_MANUAL_ATTRIBS =
      PVR_TIMER_TYPE_IS_MANUAL | PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME | PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY | PVR_TIMER_TYPE_SUPPORTS_LIFETIME;

  static const unsigned int TIMER_EPG_ATTRIBS =
      PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN | PVR_TIMER_TYPE_SUPPORTS_PRIORITY |
      PVR_TIMER_TYPE_SUPPORTS_LIFETIME;

  static const unsigned int TIMER_KEYWORD_ATTRIBS =
      PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH | PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH | PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_PRIORITY | PVR_TIMER_TYPE_SUPPORTS_LIFETIME;

  static const unsigned int TIMER_REPEATING_MANUAL_ATTRIBS = PVR_TIMER_TYPE_IS_REPEATING |
                                                             PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
                                                             PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_REPEATING_EPG_ATTRIBS =
      PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_REQUIRES_EPG_SERIES_ON_CREATE |
      PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME | PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES | PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_REPEATING_KEYWORD_ATTRIBS =
      PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_CHILD_ATTRIBUTES = PVR_TIMER_TYPE_SUPPORTS_START_TIME |
                                                     PVR_TIMER_TYPE_SUPPORTS_END_TIME |
                                                     PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES;

  /* Timer types definition.*/
  static std::vector<std::unique_ptr<TimerType>> timerTypes;
  if (timerTypes.size() == 0)
  {
    timerTypes.emplace_back(
        /* One-shot manual (time and channel based) */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_MANUAL,
            /* Attributes. */
            TIMER_MANUAL_ATTRIBS,
            /* Description. */
            kodi::addon::GetLocalizedString(30131), // "One time (manual)",
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));

    timerTypes.emplace_back(
        /* One-shot epg based */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_EPG,
            /* Attributes. */
            TIMER_EPG_ATTRIBS,
            /* Description. */
            kodi::addon::GetLocalizedString(30132), // "One time (guide)",
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));

    timerTypes.emplace_back(
        /* One shot Keyword based */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_KEYWORD,
            /* Attributes. */
            TIMER_KEYWORD_ATTRIBS,
            /* Description. */
            kodi::addon::GetLocalizedString(30133), // "One time (wishlist)"
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));

    timerTypes.emplace_back(
        /* Read-only one-shot for timers generated by timerec */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_MANUAL_CHILD,
            /* Attributes. */
            TIMER_MANUAL_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
            /* Description. */
            kodi::addon::GetLocalizedString(30130), // "Created by Repeating Timer"
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));

    timerTypes.emplace_back(
        /* Read-only one-shot for timers generated by autorec */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_EPG_CHILD,
            /* Attributes. */
            TIMER_EPG_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
            /* Description. */
            kodi::addon::GetLocalizedString(30130), // "Created by Repeating Timer"
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));

    timerTypes.emplace_back(
        /* One shot Keyword based */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_KEYWORD_CHILD,
            /* Attributes. */
            TIMER_KEYWORD_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
            /* Description. */
            kodi::addon::GetLocalizedString(30130), // "Created by Repeating Timer"
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));

    timerTypes.emplace_back(
        /* Repeating manual (time and channel based) Parent */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_REPEATING_MANUAL,
            /* Attributes. */
            TIMER_MANUAL_ATTRIBS | TIMER_REPEATING_MANUAL_ATTRIBS,
            /* Description. */
            kodi::addon::GetLocalizedString(30134), // "Repeating (manual)"
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));

    timerTypes.emplace_back(
        /* Repeating epg based Parent*/
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_REPEATING_EPG,
            /* Attributes. */
            TIMER_EPG_ATTRIBS | TIMER_REPEATING_EPG_ATTRIBS,
            /* Description. */
            kodi::addon::GetLocalizedString(30135), // "Repeating (guide)"
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));

    timerTypes.emplace_back(
        /* Repeating Keyword (Generic) based */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_REPEATING_KEYWORD,
            /* Attributes. */
            TIMER_KEYWORD_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
            /* Description. */
            kodi::addon::GetLocalizedString(30136), // "Repeating (wishlist)"
            /* Values definitions for attributes. */
            priorityValues, _defaultPriority, lifetimeValues, _defaultLiftetime,
            recordingLimitValues, _defaultLimit, showTypeValues, _defaultShowType)));
  }

  /* Copy data to target array. */
  for (auto it = timerTypes.begin(); it != timerTypes.end(); ++it)
    types.emplace_back(**it);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetChannelsAmount(int& amount)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  amount = _socketClient.GetInt("GetChannelCount", true);
  return PVR_ERROR_NO_ERROR;
}

// test for returned error vector from server, handle accompanying messages if any
bool isServerError(std::vector<std::string> results)
{
  if (results[0] == "error")
  {
    if (results.size() > 1 && results[1].length() != 0)
    {
      kodi::Log(ADDON_LOG_ERROR, results[1].c_str()); // log more info on error
    }
    if (results.size() > 2 != 0)
    {
      int errorID = atoi(results[2].c_str());
      if (errorID != 0)
      {
        std::string errStr = kodi::addon::GetLocalizedString(errorID);
        kodi::QueueNotification(QUEUE_ERROR, "", errStr);
      }
    }
    return true;
  }
  else
    return false;
}

// look at result vector from server and perform any updates requested
void Pvr2Wmc::TriggerUpdates(std::vector<std::string> results)
{
  for (const auto& response : results)
  {
    std::vector<std::string> v = Utils::Split(response, "|"); // split to unpack string

    if (v.size() < 1)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for Triggers/Message");
      return;
    }

    if (v[0] == "updateTimers")
      kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
    else if (v[0] == "updateRecordings")
      kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
    else if (v[0] == "updateChannels")
      kodi::addon::CInstancePVRClient::TriggerChannelUpdate();
    else if (v[0] == "updateChannelGroups")
      kodi::addon::CInstancePVRClient::TriggerChannelGroupsUpdate();
    else if (v[0] == "updateEPGForChannel")
    {
      if (v.size() > 1)
      {
        unsigned int channelUid = strtoul(v[1].c_str(), nullptr, 10);
        kodi::addon::CInstancePVRClient::TriggerEpgUpdate(channelUid);
      }
    }
    else if (v[0] == "message")
    {
      if (v.size() < 4)
      {
        kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for Message");
        return;
      }

      kodi::Log(ADDON_LOG_INFO, "Received message from backend: %s", response.c_str());
      std::string infoStr;

      // Get notification level
      int level = atoi(v[1].c_str());
      if (level < QUEUE_INFO)
      {
        level = QUEUE_INFO;
      }
      else if (level > QUEUE_ERROR)
      {
        level = QUEUE_ERROR;
      }

      // Get localised string for this stringID
      int stringId = atoi(v[2].c_str());
      infoStr = kodi::addon::GetLocalizedString(stringId);

      // Use text from backend if stringID not found
      if (infoStr == "")
      {
        infoStr = v[3];
      }

      // Send XBMC Notification (support up to 4 parameter replaced arguments from the backend)
      if (v.size() == 4)
      {
        kodi::QueueFormattedNotification((QueueMsg)level, infoStr.c_str());
      }
      else if (v.size() == 5)
      {
        kodi::QueueFormattedNotification((QueueMsg)level, infoStr.c_str(), v[4].c_str());
      }
      else if (v.size() == 6)
      {
        kodi::QueueFormattedNotification((QueueMsg)level, infoStr.c_str(), v[4].c_str(),
                                         v[5].c_str());
      }
      else if (v.size() == 7)
      {
        kodi::QueueFormattedNotification((QueueMsg)level, infoStr.c_str(), v[4].c_str(),
                                         v[5].c_str(), v[6].c_str());
      }
      else
      {
        kodi::QueueFormattedNotification((QueueMsg)level, infoStr.c_str(), v[4].c_str(),
                                         v[5].c_str(), v[6].c_str(), v[7].c_str());
      }
    }
  }
}

void Pvr2Wmc::ExtractDriveSpace(std::vector<std::string> results)
{
  for (const auto& response : results)
  {
    std::vector<std::string> v = Utils::Split(response, "|"); // split to unpack string

    if (v.size() < 1)
    {
      continue;
    }

    if (v[0] == "driveSpace")
    {
      if (v.size() > 1)
      {

        uint64_t totalSpace = strtoull(v[1].c_str(), nullptr, 10);
        uint64_t freeSpace = strtoull(v[2].c_str(), nullptr, 10);
        uint64_t usedSpace = strtoull(v[3].c_str(), nullptr, 10);
        _diskTotal = totalSpace / 1024;
        _diskUsed = usedSpace / 1024;
      }
    }
  }
}

// xbmc call: get all channels for either tv or radio
PVR_ERROR Pvr2Wmc::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& channelResults)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string request;
  request = Utils::Format("GetChannels|%s", radio ? "True" : "False");
  std::vector<std::string> results = _socketClient.GetVector(request, true);

  for (const auto& response : results)
  {
    kodi::addon::PVRChannel xChannel;

    std::vector<std::string> v = Utils::Split(response, "|");
    // packing: id, bradio, c.OriginalNumber, c.CallSign, c.IsEncrypted, imageStr, c.IsBlocked

    if (v.size() < 9)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for channel data");
      continue;
    }

    // Populate Channel (and optionally subchannel if one was provided)
    std::vector<std::string> c = Utils::Split(v[7], ".");
    if (c.size() > 1)
    {
      xChannel.SetChannelNumber(atoi(c[0].c_str()));
      xChannel.SetSubChannelNumber(atoi(c[1].c_str()));
    }
    else
    {
      xChannel.SetChannelNumber(atoi(v[2].c_str()));
    }

    xChannel.SetUniqueId(strtoul(v[0].c_str(), nullptr, 10)); // convert to unsigned int
    xChannel.SetIsRadio(Utils::Str2Bool(v[1]));
    xChannel.SetChannelName(v[3]);
    xChannel.SetEncryptionSystem(Utils::Str2Bool(v[4]));
    if (v[5].compare("NULL") != 0) // if icon path is not null
      xChannel.SetIconPath(v[5]);
    xChannel.SetIsHidden(Utils::Str2Bool(v[6]));

    channelResults.Add(xChannel);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetChannelGroupsAmount(int& amount)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  amount = _socketClient.GetInt("GetChannelGroupCount", true);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetChannelGroups(bool radio,
                                    kodi::addon::PVRChannelGroupsResultSet& groupResults)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string request;
  request = Utils::Format("GetChannelGroups|%s", radio ? "True" : "False");
  std::vector<std::string> results = _socketClient.GetVector(request, true);

  for (const auto& response : results)
  {
    kodi::addon::PVRChannelGroup xGroup;

    std::vector<std::string> v = Utils::Split(response, "|");

    if (v.size() < 1)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for channel group data");
      continue;
    }

    xGroup.SetIsRadio(radio);
    xGroup.SetGroupName(v[0]);

    // PVR API 1.9.6 adds Position field
    if (v.size() >= 2)
    {
      xGroup.SetPosition(atoi(v[1].c_str()));
    }

    groupResults.Add(xGroup);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetChannelGroupMembers(
    const kodi::addon::PVRChannelGroup& group,
    kodi::addon::PVRChannelGroupMembersResultSet& groupMemberResults)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string request;
  request = Utils::Format("GetChannelGroupMembers|%s|%s", group.GetIsRadio() ? "True" : "False",
                          group.GetGroupName().c_str());
  std::vector<std::string> results = _socketClient.GetVector(request, true);

  for (const auto& response : results)
  {
    kodi::addon::PVRChannelGroupMember xGroupMember;

    std::vector<std::string> v = Utils::Split(response, "|");

    if (v.size() < 2)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for channel group member data");
      continue;
    }

    xGroupMember.SetGroupName(group.GetGroupName());
    xGroupMember.SetChannelUniqueId(strtoul(v[0].c_str(), nullptr, 10)); // convert to unsigned int
    xGroupMember.SetChannelNumber(atoi(v[1].c_str()));

    groupMemberResults.Add(xGroupMember);
  }

  return PVR_ERROR_NO_ERROR;
}

namespace
{

std::string ParseAsW3CDateString(time_t time)
{
  std::tm* tm = std::localtime(&time);
  char buffer[16];
  std::strftime(buffer, 16, "%Y-%m-%d", tm);

  return buffer;
}

} // unnamed namespace

PVR_ERROR Pvr2Wmc::GetEPGForChannel(int channelUid,
                                    time_t start,
                                    time_t end,
                                    kodi::addon::PVREPGTagsResultSet& epgResults)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string request;
  request = Utils::Format("GetEntries|%u|%lld|%lld", channelUid, (long long)start,
                          (long long)end); // build the request string

  std::vector<std::string> results =
      _socketClient.GetVector(request, true); // get entries from server

  for (const auto& response : results)
  {
    kodi::addon::PVREPGTag xEpg;
    std::vector<std::string> v = Utils::Split(response, "|"); // split to unpack string

    if (v.size() < 16)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for epg data");
      continue;
    }

    //	e.Id, e.Program.Title, c.OriginalNumber, start_t, end_t,
    //	e.Program.ShortDescription, e.Program.Description,
    //	origAirDate, e.TVRating, e.Program.StarRating,
    //	e.Program.SeasonNumber, e.Program.EpisodeNumber,
    //	e.Program.EpisodeTitle
    //	(MB3 fields) channelID, audioFormat, GenreString, ProgramType
    //	actors, directors, writers, year, movieID
    xEpg.SetUniqueChannelId(channelUid); // assign unique channel ID
    xEpg.SetUniqueBroadcastId(atoi(v[0].c_str())); // entry ID
    xEpg.SetTitle(v[1]); // entry title
    xEpg.SetStartTime(atol(v[3].c_str())); // start time
    xEpg.SetEndTime(atol(v[4].c_str())); // end time
    xEpg.SetPlotOutline(
        v[5]); // short plot description (currently using episode name, if there is one)
    xEpg.SetPlot(v[6]); // long plot description
    time_t firstAired = atol(v[7].c_str()); // orig air date
    std::string strFirstAired((firstAired > 0) ? ParseAsW3CDateString(firstAired) : "");
    xEpg.SetFirstAired(strFirstAired);
    xEpg.SetParentalRating(atoi(v[8].c_str())); // tv rating
    xEpg.SetStarRating(atoi(v[9].c_str())); // star rating
    xEpg.SetSeriesNumber(atoi(v[10].c_str())); // season (?) number
    xEpg.SetEpisodeNumber(atoi(v[11].c_str())); // episode number
    if (xEpg.GetSeriesNumber() == 0 && xEpg.GetEpisodeNumber() == 0)
    {
      xEpg.SetSeriesNumber(EPG_TAG_INVALID_SERIES_EPISODE);
      xEpg.SetEpisodeNumber(EPG_TAG_INVALID_SERIES_EPISODE);
    }
    xEpg.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE);
    xEpg.SetGenreType(atoi(v[12].c_str())); // season (?) number
    xEpg.SetGenreSubType(atoi(v[13].c_str())); // general genre type
    xEpg.SetIconPath(v[14]); // the icon url
    xEpg.SetEpisodeName(v[15]); // the episode name
    xEpg.SetGenreDescription("");

    // Kodi PVR API 1.9.6 adds new EPG fields
    if (v.size() >= 25)
    {
      xEpg.SetCast(v[20]);
      xEpg.SetDirector(v[21]);
      xEpg.SetWriter(v[22]);
      xEpg.SetYear(atoi(v[23].c_str()));
      xEpg.SetIMDBNumber(v[24]);
    }

    // Kodi PVR API 4.1.0 adds new EPG iFlags field
    unsigned int flags = 0;
    if (v.size() >= 26)
    {
      if (Utils::Str2Bool(v[25]))
      {
        flags |= EPG_TAG_FLAG_IS_SERIES;
      }
    }
    xEpg.SetFlags(flags);

    epgResults.Add(xEpg);
  }
  return PVR_ERROR_NO_ERROR;
}


// timer functions -------------------------------------------------------------
PVR_ERROR Pvr2Wmc::GetTimersAmount(int& amount)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  amount = _socketClient.GetInt("GetTimerCount", true);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::AddTimer(const kodi::addon::PVRTimer& timer)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  // Send request to ServerWMC
  std::string command = "";
  command = "SetTimerKodi" + Timer2String(timer); // convert timer to string

  std::vector<std::string> results =
      _socketClient.GetVector(command, false); // get results from server

  kodi::addon::CInstancePVRClient::TriggerTimerUpdate(); // update timers regardless of whether
      // there is an error

  if (isServerError(results))
  {
    return PVR_ERROR_SERVER_ERROR;
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG, "recording added for timer '%s', with rec state %s",
              timer.GetTitle().c_str(), results[0].c_str());

    if (results.size() > 1) // if there is extra results sent from server...
    {
      for (const auto& response : results)
      {
        std::vector<std::string> splitResult =
            Utils::Split(response, "|"); // split to unpack extra info on each result
        std::string infoStr;

        if (splitResult[0] == "recordingNow") // recording is active now
        {
          kodi::Log(ADDON_LOG_DEBUG, "timer recording is in progress");
        }
        else if (splitResult[0] ==
                 "recordingNowTimedOut") // swmc timed out waiting for the recording to start
        {
          kodi::Log(ADDON_LOG_DEBUG, "server timed out waiting for in-progress recording to start");
        }
        else if (splitResult[0] ==
                 "recordingChannel") // service picked a different channel for timer
        {
          kodi::Log(ADDON_LOG_DEBUG, "timer channel changed by wmc to '%s'",
                    splitResult[1].c_str());
          // build info string and notify user of channel change
          infoStr = kodi::addon::GetLocalizedString(30009) + splitResult[1];
          kodi::QueueNotification(QUEUE_WARNING, "", infoStr);
        }
        else if (splitResult[0] ==
                 "recordingTime") // service picked a different start time for timer
        {
          kodi::Log(ADDON_LOG_DEBUG, "timer start time changed by wmc to '%s'",
                    splitResult[1].c_str());
          // build info string and notify user of time change
          infoStr = kodi::addon::GetLocalizedString(30010) + splitResult[1];
          kodi::QueueNotification(QUEUE_WARNING, "", infoStr);
        }
        else if (splitResult[0] ==
                 "increasedEndTime") // end time has been increased on an instant record
        {
          kodi::Log(ADDON_LOG_DEBUG, "instant record end time increased by '%s' minutes",
                    splitResult[1].c_str());
          // build info string and notify user of time increase
          infoStr = kodi::addon::GetLocalizedString(30013) + splitResult[1] + " min";
          kodi::QueueNotification(QUEUE_INFO, "", infoStr);
        }
      }
    }

    return PVR_ERROR_NO_ERROR;
  }
}

std::string Pvr2Wmc::Timer2String(const kodi::addon::PVRTimer& xTmr)
{
  std::string tStr;

  bool bRepeating =
      xTmr.GetTimerType() >= TIMER_REPEATING_MIN && xTmr.GetTimerType() <= TIMER_REPEATING_MAX;
  bool bKeyword = xTmr.GetTimerType() == TIMER_REPEATING_KEYWORD ||
                  xTmr.GetTimerType() == TIMER_ONCE_KEYWORD ||
                  xTmr.GetTimerType() == TIMER_ONCE_KEYWORD_CHILD;
  bool bManual = xTmr.GetTimerType() == TIMER_ONCE_MANUAL ||
                 xTmr.GetTimerType() == TIMER_ONCE_MANUAL_CHILD ||
                 xTmr.GetTimerType() == TIMER_REPEATING_MANUAL;

  // was ("|%d|%d|%d|%d|%d|%s|%d|%d|%d|%d|%d",
  tStr = Utils::Format("|%u|%d|%lld|%lld|%d|%s|%d|%u|%u|%d|%u", xTmr.GetClientIndex(),
                       xTmr.GetClientChannelUid(), (long long)xTmr.GetStartTime(),
                       (long long)xTmr.GetEndTime(), PVR_TIMER_STATE_NEW, // 0-4
                       xTmr.GetTitle().c_str(), xTmr.GetPriority(), xTmr.GetMarginStart(),
                       xTmr.GetMarginEnd(), bRepeating, // 5-9
                       xTmr.GetEPGUid()); // 10

  // Append extra fields from Kodi 16
  std::string extra;
  // was ("|%d|%d|%d|%d|%d|%d|%s|%d|%d",
  extra =
      Utils::Format("|%u|%d|%u|%d|%d|%d|%s|%d|%d", xTmr.GetPreventDuplicateEpisodes(),
                    xTmr.GetStartAnyTime(), xTmr.GetWeekdays(), // 11-13 param
                    xTmr.GetLifetime(), bKeyword, xTmr.GetFullTextEpgSearch(),
                    xTmr.GetEPGSearchString().c_str(), xTmr.GetMaxRecordings(), bManual); // 14-19
  tStr.append(extra);

  return tStr;
}

PVR_ERROR Pvr2Wmc::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  bool bRepeating =
      timer.GetTimerType() >= TIMER_REPEATING_MIN && timer.GetTimerType() <= TIMER_REPEATING_MAX;

  std::string command = "DeleteTimerKodi";
  command = Utils::Format("DeleteTimerKodi|%u|%d", timer.GetClientIndex(), bRepeating);

  std::vector<std::string> results =
      _socketClient.GetVector(command, false); // get results from server

  kodi::addon::CInstancePVRClient::TriggerTimerUpdate(); // update timers regardless of whether
      // there is an error

  if (isServerError(results)) // did the server do it?
  {
    return PVR_ERROR_SERVER_ERROR;
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG, "deleted timer '%s', with rec state %s", timer.GetTitle().c_str(),
              results[0].c_str());
    return PVR_ERROR_NO_ERROR;
  }
}

PVR_ERROR Pvr2Wmc::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::vector<std::string> responsesSeries = _socketClient.GetVector("GetSeriesTimers", true);
  for (const auto& response : responsesSeries)
  {
    kodi::addon::PVRTimer xTmr;

    std::vector<std::string> v = Utils::Split(response, "|"); // split to unpack string
    if (v.size() < 24)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for SeriesTimer data");
      continue;
    }

    xTmr.SetTimerType(PVR_TIMER_TYPE_NONE);
    // [0] Timer ID (need UINT32 value, see [21])
    // [1] Title (superceded by [17] Timer Name)
    xTmr.SetClientChannelUid(atoi(v[2].c_str())); // [2] channel id
    xTmr.SetEPGUid(
        strtoul(v[3].c_str(), nullptr, 10)); // [3] epg ID (same as client ID, except for a 'manual' record)
    xTmr.SetSummary(v[4]); // [4] currently set to description
    xTmr.SetStartTime(atoi(v[5].c_str())); // [5] start time
    xTmr.SetEndTime(atoi(v[6].c_str())); // [6] end time
    xTmr.SetMarginStart(strtoul(v[7].c_str(), nullptr, 10)); // [7] rec margin at start (sec)
    xTmr.SetMarginEnd(strtoul(v[8].c_str(), nullptr, 10)); // [8] rec margin at end (sec)
        // [9] isPreMarginRequired
        // [10] isPostMarginRequired
        // [11] WMC Priority (need Kodi compatible value, see [26])
        // [12] NewEpisodesOnly (superceded by RunType)
    if (Utils::Str2Bool(v[13].c_str())) // [13] Any Channel
    {
      xTmr.SetClientChannelUid(0);
    }
    if (Utils::Str2Bool(v[14].c_str())) // [14] Any Time
    {
      xTmr.SetStartAnyTime(true);
      xTmr.SetEndAnyTime(true);
    }
    xTmr.SetWeekdays(
        strtoul(v[15].c_str(), nullptr, 10)); // [15] DaysOfWeek (converted to Kodi values in the backend)
    xTmr.SetState((PVR_TIMER_STATE)atoi(v[16].c_str())); // [16] current state of timer
    xTmr.SetTitle(v[17]); // [17] timer name
    xTmr.SetGenreType(atoi(v[18].c_str())); // [18] genre ID
    xTmr.SetGenreSubType(atoi(v[19].c_str())); // [19] sub genre ID
    xTmr.SetPreventDuplicateEpisodes(strtoul(v[20].c_str(), nullptr, 10)); // [20] WMC RunType
    xTmr.SetClientIndex(strtoul(v[21].c_str(), nullptr, 10)); // [21] Timer ID (in UINT32 form)
    xTmr.SetEPGSearchString(v[22]); // [22] Keyword Search
    xTmr.SetFullTextEpgSearch(Utils::Str2Bool(v[23])); // [23] Keyword is FullText
    xTmr.SetLifetime(atoi(v[24].c_str())); // [24] Lifetime
    xTmr.SetMaxRecordings(atoi(v[25].c_str())); // [25] Maximum Recordings (Recording Limit)
    xTmr.SetPriority(atoi(v[26].c_str())); // [26] Priority (in Kodi enum value form)

    // Determine TimerType
    bool hasKeyword = !xTmr.GetEPGSearchString().empty();
    bool hasEPG = (xTmr.GetEPGUid() != PVR_TIMER_NO_EPG_UID);
    xTmr.SetTimerType(hasKeyword ? TIMER_REPEATING_KEYWORD
                                 : hasEPG ? TIMER_REPEATING_EPG : TIMER_REPEATING_MANUAL);

    results.Add(xTmr);
  }

  std::vector<std::string> responsesTimers = _socketClient.GetVector("GetTimers", true);
  for (const auto& response : responsesTimers)
  {
    kodi::addon::PVRTimer xTmr;

    std::vector<std::string> v = Utils::Split(response, "|"); // split to unpack string
    // eId, chId, start_t, end_t, pState,
    // rp.Program.Title, ""/*recdir*/, rp.Program.EpisodeTitle/*summary?*/, rp.Priority,
    // rp.Request.IsRecurring, eId, preMargin, postMargin, genre, subgenre

    if (v.size() < 24)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for timer data");
      continue;
    }

    xTmr.SetTimerType(PVR_TIMER_TYPE_NONE);
    xTmr.SetClientIndex(strtoul(v[0].c_str(), nullptr, 10)); // [0] Timer ID
    xTmr.SetClientChannelUid(atoi(v[1].c_str())); // [1] channel id
    xTmr.SetStartTime(atoi(v[2].c_str())); // [2] start time
    xTmr.SetEndTime(atoi(v[3].c_str())); // [3] end time
    xTmr.SetState((PVR_TIMER_STATE)atoi(v[4].c_str())); // [4] current state of time
    xTmr.SetTitle(v[5]); // [5] timer name (set to same as Program title)
    xTmr.SetDirectory(v[6]); // [6] rec directory
    xTmr.SetSummary(v[7]); // [7] set to program description
        // [8] WMC Priority (need Kodi compatible value, see [26])
        // [9] IsRecurring
    xTmr.SetEPGUid(strtoul(v[10].c_str(), nullptr, 10)); // [10] epg ID
    xTmr.SetMarginStart(strtoul(v[11].c_str(), nullptr, 10)); // [11] rec margin at start (sec)
    xTmr.SetMarginEnd(strtoul(v[12].c_str(), nullptr, 10)); // [12] rec margin at end (sec)
    xTmr.SetGenreType(atoi(v[13].c_str())); // [13] genre ID
    xTmr.SetGenreSubType(atoi(v[14].c_str())); // [14] sub genre ID
        // [15] epg ID (duplicated from [9] for some reason)
        // [16] Parent Series ID (need in UINT32 form, see [23])
        // [17] isPreMarginRequired
        // [18] isPostMarginRequired
    xTmr.SetPreventDuplicateEpisodes(strtoul(v[19].c_str(), nullptr, 10)); // [19] WMC runType
    if (Utils::Str2Bool(v[20].c_str())) // [20] Any Channel
    {
      // As this is a child instance recording, we want to preserve the actual channel
    }
    if (Utils::Str2Bool(v[21].c_str())) // [21] Any Time
    {
      // As this is a child instance recording, we want to preserve the actual start/finish times
    }
    xTmr.SetWeekdays(
        strtoul(v[22].c_str(), nullptr, 10)); // [22] DaysOfWeek (converted to Kodi values in the backend)
    xTmr.SetParentClientIndex(strtoul(v[23].c_str(), nullptr, 10)); // [23] Parent Series ID (in UINT32 form)
    xTmr.SetLifetime(atoi((v[24].c_str()))); // [24] Lifetime
    xTmr.SetMaxRecordings(atoi(v[25].c_str())); // [25] Maximum Recordings (Recording Limit)
    xTmr.SetPriority(atoi(v[26].c_str())); // [26] Priority (in Kodi enum value form)
    xTmr.SetEPGSearchString(v[27]); // [27] Keyword Search
    xTmr.SetFullTextEpgSearch(Utils::Str2Bool(v[28].c_str())); // [28] Keyword is FullText

    // Determine TimerType
    bool hasParent = (xTmr.GetParentClientIndex() != 0);
    bool hasKeyword = !xTmr.GetEPGSearchString().empty();
    bool hasEPG = (xTmr.GetEPGUid() != PVR_TIMER_NO_EPG_UID);
    if (hasParent)
    {
      xTmr.SetTimerType(hasKeyword ? TIMER_ONCE_KEYWORD_CHILD
                                   : hasEPG ? TIMER_ONCE_EPG_CHILD : TIMER_ONCE_MANUAL_CHILD);
    }
    else
    {
      xTmr.SetTimerType(hasKeyword ? TIMER_ONCE_KEYWORD
                                   : hasEPG ? TIMER_ONCE_EPG : TIMER_ONCE_MANUAL);
    }

    results.Add(xTmr);
  }

  // check time since last time Recordings were updated, update if it has been awhile
  std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
  if (static_cast<int>(
          std::chrono::duration_cast<std::chrono::seconds>(now - _lastRecordingUpdateTime).count() >
          120))
  {
    kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
  }
  return PVR_ERROR_NO_ERROR;
}

// recording functions ------------------------------------------------------------------------
PVR_ERROR Pvr2Wmc::GetRecordingsAmount(bool deleted, int& amount)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  if (!deleted)
    amount = _socketClient.GetInt("GetRecordingsAmount", true);
  return PVR_ERROR_NO_ERROR;
}

// recording file  functions
PVR_ERROR Pvr2Wmc::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::vector<std::string> responses = _socketClient.GetVector("GetRecordings", true);

  for (const auto& response : responses)
  {
    kodi::addon::PVRRecording xRec;

    xRec.SetSeriesNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
    xRec.SetEpisodeNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);

    std::vector<std::string> v = Utils::Split(response, "|"); // split to unpack string

    // r.Id, r.Program.Title, r.FileName, recDir, plotOutline,
    // plot, r.Channel.CallSign, ""/*icon path*/, ""/*thumbnail path*/, ToTime_t(r.RecordingTime),
    // duration, r.RequestedProgram.Priority, r.KeepLength.ToString(), genre, subgenre, ResumePos
    // fields 16 - 23 used by MB3, 24 PlayCount

    if (v.size() < 16)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Wrong number of fields xfered for recording data");
      continue;
    }

    xRec.SetRecordingId(v[0]);
    xRec.SetTitle(v[1]);
    xRec.SetDirectory(v[3]);
    xRec.SetPlotOutline(v[4]);
    xRec.SetPlot(v[5]);
    xRec.SetChannelName(v[6]);
    xRec.SetIconPath(v[7]);
    xRec.SetThumbnailPath(v[8]);
    xRec.SetRecordingTime(atoi(v[9].c_str()));
    xRec.SetDuration(atoi(v[10].c_str()));
    xRec.SetPriority(atoi(v[11].c_str()));
    xRec.SetLifetime(atoi(v[12].c_str()));
    xRec.SetGenreType(atoi(v[13].c_str()));
    xRec.SetGenreSubType(atoi(v[14].c_str()));
    if (_addon.GetSettings().GetEnableMultiResume())
    {
      xRec.SetLastPlayedPosition(atoi(v[15].c_str()));
      if (v.size() > 24)
      {
        xRec.SetPlayCount(atoi(v[24].c_str()));
      }
    }

    // Kodi PVR API 1.9.5 adds EPG ID field
    if (v.size() > 19)
    {
      xRec.SetEPGEventId(strtoul(v[18].c_str(), nullptr, 10));
    }

    // Kodi PVR API 5.0.0 adds EPG ID field
    if (v.size() > 18)
    {
      xRec.SetChannelUid(atoi(v[17].c_str()));
    }
    else
    {
      xRec.SetChannelUid(PVR_CHANNEL_INVALID_UID);
    }

    /* TODO: PVR API 5.1.0: Implement this */
    xRec.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_UNKNOWN);

    results.Add(xRec);
  }

  _lastRecordingUpdateTime = std::chrono::high_resolution_clock::now();

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string command;
  command = Utils::Format("DeleteRecording|%s|%s|%s", recording.GetRecordingId().c_str(),
                          recording.GetTitle().c_str(), "");

  std::vector<std::string> results =
      _socketClient.GetVector(command, false); // get results from server


  if (isServerError(results)) // did the server do it?
  {
    return PVR_ERROR_NO_ERROR; // report "no error" so our error shows up
  }
  else
  {
    TriggerUpdates(results);
    // kodi::addon::CInstancePVRClient::TriggerRecordingUpdate(); // tell xbmc to update recording
    // display
    kodi::Log(ADDON_LOG_DEBUG, "deleted recording '%s'", recording.GetTitle().c_str());

    // if (results.size() == 2 && results[0] == "updateTimers")	// if deleted recording was actually
    // recording a the time
    //  kodi::addon::CInstancePVRClient::TriggerTimerUpdate(); // update timer display too

    return PVR_ERROR_NO_ERROR;
  }
}

PVR_ERROR Pvr2Wmc::RenameRecording(const kodi::addon::PVRRecording& recording)
{
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string command; // = format("RenameRecording|%s|%s", recording.GetRecordingId().c_str(),
      // recording.GetTitle().c_str());
  command = Utils::Format("RenameRecording|%s|%s", recording.GetRecordingId().c_str(),
                          recording.GetTitle().c_str());

  std::vector<std::string> results =
      _socketClient.GetVector(command, false); // get results from server

  if (isServerError(results)) // did the server do it?
  {
    return PVR_ERROR_NO_ERROR; // report "no error" so our error shows up
  }
  else
  {
    TriggerUpdates(results);
    kodi::Log(ADDON_LOG_DEBUG, "deleted recording '%s'", recording.GetTitle().c_str());
    return PVR_ERROR_NO_ERROR;
  }
}

// set the recording resume position in the wmc database
PVR_ERROR Pvr2Wmc::SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording,
                                                  int lastplayedposition)
{
  if (!_addon.GetSettings().GetEnableMultiResume())
    return PVR_ERROR_NOT_IMPLEMENTED;
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string command;
  command = Utils::Format("SetResumePosition|%s|%d", recording.GetRecordingId().c_str(),
                          lastplayedposition);

  std::vector<std::string> results = _socketClient.GetVector(command, true);
  kodi::addon::CInstancePVRClient::TriggerRecordingUpdate(); // this is needed to get the new resume
      // point actually used by the player
      // (xbmc bug)
  return PVR_ERROR_NO_ERROR;
}

// get the rercording resume position from the wmc database
// note: although this resume point time will be displayed to the user in the gui (in the resume
// dlog) the return value is ignored by the xbmc player.  That's why TriggerRecordingUpdate is
// required in the setting above
PVR_ERROR Pvr2Wmc::GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording,
                                                  int& position)
{
  if (!_addon.GetSettings().GetEnableMultiResume())
    return PVR_ERROR_NOT_IMPLEMENTED;
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string command;
  command = Utils::Format("GetResumePosition|%s", recording.GetRecordingId().c_str());
  position = _socketClient.GetInt(command, true);
  return PVR_ERROR_NO_ERROR;
}

// set the recording playcount in the wmc database
PVR_ERROR Pvr2Wmc::SetRecordingPlayCount(const kodi::addon::PVRRecording& recording, int count)
{
  if (!_addon.GetSettings().GetEnableMultiResume())
    return PVR_ERROR_NOT_IMPLEMENTED;
  if (IsServerDown())
    return PVR_ERROR_SERVER_ERROR;

  std::string command;
  command = Utils::Format("SetPlayCount|%s|%d", recording.GetRecordingId().c_str(), count);
  std::vector<std::string> results = _socketClient.GetVector(command, true);
  if (count <= 0)
    kodi::addon::CInstancePVRClient::TriggerRecordingUpdate(); // this is needed to get the new play
        // count actually used by the player
        // (xbmc bug)
  return PVR_ERROR_NO_ERROR;
}


std::string Pvr2Wmc::Channel2String(const kodi::addon::PVRChannel& xCh)
{
  // packing: id, bradio, c.OriginalNumber, c.CallSign, c.IsEncrypted, imageStr, c.IsBlocked
  std::string chStr;
  chStr = Utils::Format("|%u|%d|%u|%s", xCh.GetUniqueId(), xCh.GetIsRadio(), xCh.GetChannelNumber(),
                        xCh.GetChannelName().c_str());
  return chStr;
}

// live/recorded stream functions --------------------------------------------------------------

bool Pvr2Wmc::OpenLiveStream(const kodi::addon::PVRChannel& channel)
{
  if (IsServerDown())
    return false;

  _lostStream = true; // init
  _readCnt = 0;
  _buffTimesCnt = 0;
  _buffTimeFILTER = 0;
  _bRecordingPlayback = false;

  CloseStream(false); // close current stream (if any)

  std::string request =
      "OpenLiveStream" + Channel2String(channel); // request a live stream using channel
  std::vector<std::string> results =
      _socketClient.GetVector(request, false); // try to open live stream, get path to stream file

  if (isServerError(results)) // test if server reported an error
  {
    return false;
  }
  else
  {
    _streamFileName = results[0]; // get path of streamed file
    _streamWTV = Utils::EndsWith(results[0], "wtv"); // true if stream file is a wtv file

    if (results.size() > 1)
      kodi::Log(ADDON_LOG_DEBUG, "OpenLiveStream> opening stream: %s",
                results[1].c_str()); // log password safe path of client if available
    else
      kodi::Log(ADDON_LOG_DEBUG, "OpenLiveStream> opening stream: %s", _streamFileName.c_str());

    // Initialise variables for starting stream at an offset
    _initialStreamResetCnt = 0;
    _initialStreamPosition = 0;

    // Check for a specified initial position and save it for the first ReadLiveStream command to
    // use
    if (results.size() > 2)
    {
      _initialStreamPosition = atoll(results[2].c_str());
    }

    _streamFile.OpenFile(_streamFileName); // open the video file for streaming, same handle

    if (!_streamFile.IsOpen()) // something went wrong
    {
      std::string lastError;
#ifdef TARGET_WINDOWS
      int errorVal = GetLastError();
      lastError = Utils::Format("Error opening stream file, Win32 error code: %d", errorVal);
#else
      lastError = "Error opening stream file";
#endif
      kodi::Log(ADDON_LOG_ERROR, lastError.c_str()); // log more info on error

      _socketClient.GetBool("StreamStartError|" + _streamFileName,
                            true); // tell server stream did not start

      return false;
    }
    else
    {
      _discardSignalStatus = false; // reset signal status discard flag
      kodi::Log(ADDON_LOG_DEBUG, "OpenLiveStream> stream file opened successfully");
    }

    _lostStream = false; // if we got to here, stream started ok, so set default values
    _lastStreamSize = 0;
    _isStreamFileGrowing = true;
    return true; // stream is up
  }
}


// read from the live stream file opened in OpenLiveStream
int Pvr2Wmc::ReadStream(unsigned char* pBuffer, unsigned int iBufferSize)
{
  if (_lostStream) // if stream has already been flagged as lost, return 0 bytes
    return 0; // after this happens a few time, xbmc will call CloseStream
  _readCnt++; // keep a count of the number of reads executed

  if (!_streamWTV) // if NOT streaming wtv, make sure stream is big enough before it is read
  {
    int timeout = 0; // reset timeout counter


    // If we are trying to skip to an initial start position (eg we are watching an existing live
    // stream in a multiple client scenario), we need to do it here, as the Seek command didnt work
    // in OpenLiveStream, XBMC just started playing from the start of the file anyway.  But once the
    // stream is open, XBMC repeatedly calls ReadLiveStream and a Seek() command done here DOES get
    // actioned.
    //
    // So the first time we come in here, we can Seek() to our desired start offset.
    //
    // However I found the XBMC demuxer makes an initial pass first and then sets the position back
    // to 0 again and makes a 2nd pass, So we actually need to Seek() to our initial start position
    // more than once.  Because there are other situations where we can end up at the start of the
    // file (such as the user rewinding) the easiest option at this point is to simply assume the
    // demuxer makes 2 passes, and to reset the Seek position twice before clearing the stored value
    // and thus no longer performing the reset.

    // Seek to initial file position if OpenLiveStream stored a starting offset and we are at
    // position 0 (start of the file)
    if (_initialStreamPosition > 0 && PositionStream() == 0)
    {
      int64_t newPosition = _streamFile.Seek(_initialStreamPosition, SEEK_SET);
      if (newPosition == _initialStreamPosition)
      {
        kodi::Log(ADDON_LOG_DEBUG,
                  "ReadLiveStream> stream file seek to initial position %llu successful",
                  _initialStreamPosition);
      }
      else
      {
        kodi::Log(
            ADDON_LOG_DEBUG,
            "ReadLiveStream> stream file seek to initial position %llu failed (new position: %llu)",
            _initialStreamPosition, newPosition);
      }

      _initialStreamResetCnt++;
      if (_initialStreamResetCnt >= 2)
      {
        _initialStreamPosition =
            0; // reset stored value to 0 once we have performed 2 resets (2 pass demuxer)
      }
    }

    int64_t currentPos = PositionStream(); // get the current file position
    int64_t fileSize = _lastStreamSize; // use the last fileSize found, rather than querying host

    if (_isStreamFileGrowing &&
        currentPos + iBufferSize > fileSize) // if its not big enough for the readsize
      fileSize = ActualFileSize(timeout); // get the current size of the stream file

    // if the stream file is growing, see if the stream file is big enough to accomodate this read
    // if its not, wait until it is
    while (_isStreamFileGrowing && currentPos + iBufferSize > fileSize)
    {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(600)); // wait a little (600ms) before we try again
      timeout++;
      fileSize = ActualFileSize(timeout); // get new file size after delay

      if (!_isStreamFileGrowing) // if streamfile is no longer growing...
      {
        if (CheckErrorOnServer()) // see if server says there is an error
        {
          _lostStream = true; // if an error was posted, close the stream down
          return -1;
        }
        else
          break; // terminate loop since stream file isn't growing no sense in waiting
      }
      else if (fileSize ==
               -1) // if fileSize -1, server is reporting an 'unkown' error with the stream
      {
        kodi::QueueNotification(
            QUEUE_ERROR, "",
            kodi::addon::GetLocalizedString(30003)); // display generic error with stream
        kodi::Log(ADDON_LOG_DEBUG, "live tv error, server reported error");
        _lostStream = true; // flag that stream is down
        return -1;
      }

      if (timeout > 50) // if after 30 sec the file has not grown big enough, timeout
      {
        _lostStream = true; // flag that stream is down
        if (currentPos == 0 && fileSize == 0) // if no data was ever read, assume no video signal
        {
          kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30004));
          kodi::Log(ADDON_LOG_DEBUG, "no video found for stream");
        }
        else // a mysterious reason caused timeout
        {
          kodi::QueueNotification(
              QUEUE_ERROR, "",
              kodi::addon::GetLocalizedString(30003)); // otherwise display generic error
          kodi::Log(ADDON_LOG_DEBUG, "live tv timed out, unknown reason");
        }
        return -1; // this makes xbmc call closelivestream
      }
    }
  } // !_streamWTV

  // finally, read data from stream file
  unsigned int lpNumberOfBytesRead = _streamFile.Read(pBuffer, iBufferSize);

  return lpNumberOfBytesRead;
}

// see if server posted an error for this client
// if server has not posted an error, return False
bool Pvr2Wmc::CheckErrorOnServer()
{
  if (!IsServerDown())
  {
    std::string request;
    request = "CheckError";
    std::vector<std::string> results =
        _socketClient.GetVector(request, true); // see if server posted an error for active stream
    return isServerError(results);
  }
  return false;
}

int64_t Pvr2Wmc::SeekStream(int64_t iPosition, int iWhence /* = SEEK_SET */)
{
  int64_t lFilePos = 0;
  if (_streamFile.IsOpen())
  {
    lFilePos = _streamFile.Seek(iPosition, iWhence);
  }
  return lFilePos;
}

// return the current file pointer position
int64_t Pvr2Wmc::PositionStream()
{
  int64_t lFilePos = -1;
  if (_streamFile.IsOpen())
  {
    lFilePos = _streamFile.GetPosition();
  }
  return lFilePos;
}

// get stream file size, querying it from server if needed
int64_t Pvr2Wmc::ActualFileSize(int count)
{
  int64_t lFileSize = 0;

  if (_lostStream) // if stream was lost, return 0 file size (may not be needed)
    return 0;

  if (!_isStreamFileGrowing) // if stream file is no longer growing, return the last stream size
  {
    lFileSize = _lastStreamSize;
  }
  else
  {
    std::string request;
    request = Utils::Format(
        "StreamFileSize|%d",
        count); // request stream size form client, passing number of consecutive queries
    lFileSize = _socketClient.GetLL(request, true); // get file size form client

    if (lFileSize < -1) // if server returns a negative file size, it means the stream file is no
        // longer growing (-1 => error)
    {
      lFileSize = -lFileSize; // make stream file length positive
      _isStreamFileGrowing = false; // flag that stream file is no longer growing
    }
    _lastStreamSize = lFileSize; // save this stream size
  }
  return lFileSize;
}

// return the length of the current stream file
int64_t Pvr2Wmc::LengthStream()
{
  if (_lastStreamSize > 0)
    return _lastStreamSize;
  return -1;
}

bool Pvr2Wmc::CloseStream(bool notifyServer /*=true*/)
{
  if (IsServerDown())
    return false;

  if (_streamFile.IsOpen()) // if file is still open, close it
    _streamFile.Close();

  _streamFileName = "";

  _lostStream = true; // for cleanliness
  _bRecordingPlayback = false;

  if (notifyServer)
  {
    return _socketClient.GetBool("CloseStream", false); // tell server to close down stream
  }
  else
    return true;
}

bool Pvr2Wmc::OpenRecordedStream(const kodi::addon::PVRRecording& recording, int64_t& streamId)
{
  if (IsServerDown())
    return false;

  _lostStream = true; // init
  _readCnt = 0;
  _buffTimesCnt = 0;
  _buffTimeFILTER = 0;
  _bRecordingPlayback = true;

  // request an active recording stream
  std::string request;
  request = Utils::Format("OpenRecordingStream|%s", recording.GetRecordingId().c_str());
  std::vector<std::string> results = _socketClient.GetVector(
      request, false); // try to open recording stream, get path to stream file

  if (isServerError(results)) // test if server reported an error
  {
    return false;
  }
  else
  {
    _streamFileName = results[0];
    _streamWTV = Utils::EndsWith(_streamFileName, "wtv"); // true if stream file is a wtv file

    // handle additional args from server
    if (results.size() > 1)
      kodi::Log(ADDON_LOG_DEBUG, "OpenRecordedStream> rec stream type: %s",
                results[1].c_str()); // either a 'passive' or 'active' WTV OR a TS file

    if (results.size() > 2)
      kodi::Log(ADDON_LOG_DEBUG, "OpenRecordedStream> opening stream: %s",
                results[2].c_str()); // log password safe path of client if available
    else
      kodi::Log(ADDON_LOG_DEBUG, "OpenRecordedStream> opening stream: %s", _streamFileName.c_str());

    _streamFile.OpenFile(_streamFileName); // open the video file for streaming, same handle

    if (!_streamFile.IsOpen()) // something went wrong
    {
      std::string lastError;
#ifdef TARGET_WINDOWS
      int errorVal = GetLastError();
      lastError = Utils::Format("Error opening stream file, Win32 error code: %d", errorVal);
#else
      lastError = "Error opening stream file";
#endif
      kodi::Log(ADDON_LOG_ERROR, lastError.c_str()); // log more info on error
      _socketClient.GetBool("StreamStartError|" + _streamFileName,
                            true); // tell server stream did not start
      return false;
    }
    else
      kodi::Log(ADDON_LOG_DEBUG, "OpenRecordedStream> stream file opened successfully");

    _lostStream = false; // stream is open
    _lastStreamSize = 0; // current size is empty
    _isStreamFileGrowing = true; // initially assume its growing
    ActualFileSize(
        0); // get initial file size from swmc, also tells it file was opened successfully

    // Initialise variables for starting stream at an offset (only used for live streams)
    _initialStreamResetCnt = 0;
    _initialStreamPosition = 0;

    return true; // if we got to here, stream started ok
  }
}

PVR_ERROR Pvr2Wmc::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
  if (!_addon.GetSettings().GetSignalEnable() || _discardSignalStatus)
  {
    return PVR_ERROR_NO_ERROR;
  }

  static kodi::addon::PVRSignalStatus cachedSignalStatus;

  // Only send request to backend every N times
  if (_signalStatusCount-- <= 0)
  {
    if (IsServerDown())
      return PVR_ERROR_SERVER_ERROR;

    // Reset count to throttle value
    _signalStatusCount = _addon.GetSettings().GetSignalThrottle();

    std::string command;
    command = "SignalStatus";

    std::vector<std::string> results =
        _socketClient.GetVector(command, true); // get results from server

    // strDeviceName, strDeviceStatus, strProvider, strService, strMux
    // iSignal, dVideoBitrate, dAudioBitrate, Error

    if (isServerError(results)) // did the server do it?
    {
      return PVR_ERROR_SERVER_ERROR; // report "no error" so our error shows up
    }
    else
    {
      if (results.size() >= 9)
      {
        cachedSignalStatus.SetAdapterName(results[0]);
        cachedSignalStatus.SetAdapterStatus(results[1]);
        cachedSignalStatus.SetProviderName(results[2]);
        cachedSignalStatus.SetServiceName(results[3]);
        cachedSignalStatus.SetMuxName(results[4]);
        cachedSignalStatus.SetSignal(static_cast<int>(atoi(results[5].c_str()) * 655.35));
        bool error = atoi(results[8].c_str()) == 1;
        if (error)
        {
          // Backend indicates it can't provide SignalStatus for this channel
          // Set flag to discard further attempts until a channel change
          _discardSignalStatus = true;
        }
      }
    }
  }

  signalStatus = cachedSignalStatus;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetStreamTimes(kodi::addon::PVRStreamTimes& times)
{
  if (_streamFile.IsOpen())
  {
    if (_buffTimesCnt >= _buffTimeFILTER) // filter queries to slow down queries to swmc
    {
      _buffTimesCnt = 0;
      std::vector<std::string> results =
          _socketClient.GetVector("GetBufferTimes", false); // get buffer status

      if (results.size() < 3)
      {
        return PVR_ERROR_SERVER_ERROR;
      }

      times.SetStartTime(atoll(results[0].c_str())); // get time_t utc of when stream was started
      times.SetPTSStart(0); // relative to the above time, time when the stream starts (?)
      times.SetPTSBegin(
          0); // how far back the buffer data goes, which is always stream start for swmc
      times.SetPTSEnd(
          atoll(results[1].c_str()) *
          STREAM_TIME_BASE); // get the current length of the live buffer or recording duration (uSec)
      _savBuffStart = times.GetStartTime(); // save values last found to filter queries
      _savBuffEnd = times.GetPTSEnd();
      _buffTimeFILTER = atol(results[2].c_str()); // get filter value from swmc
    }
    else
    {
      // if filtering, used saved values
      times.SetStartTime(_savBuffStart);
      times.SetPTSStart(0);
      times.SetPTSBegin(0);
      times.SetPTSEnd(_savBuffEnd);
      _buffTimesCnt++; // increment how many times saved values were used
    }
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR Pvr2Wmc::GetRecordingEdl(const kodi::addon::PVRRecording& recording,
                                   std::vector<kodi::addon::PVREDLEntry>& edl)
{
  if (!_streamFileName.empty()) // read the edl for the current stream file
  {
    // see if edl file for currently streaming recording exists
    std::string theEdlFile = _streamFileName;
    // swap .wtv extension for .edl
    std::string::size_type result = theEdlFile.find_last_of('.');
    if (std::string::npos != result)
      theEdlFile.erase(result);
    else
    {
      kodi::Log(ADDON_LOG_DEBUG, "File extender error: '%s'", theEdlFile.c_str());
      return PVR_ERROR_FAILED;
    }
    theEdlFile.append(".edl");

    kodi::Log(ADDON_LOG_DEBUG, "Opening EDL file: '%s'", theEdlFile.c_str());

    kodi::vfs::CFile fileHandle;
    if (fileHandle.OpenFile(theEdlFile))
    {
      std::string svals;
      while (fileHandle.ReadLine(svals))
      {
        size_t nidx = svals.find_last_not_of("\r");
        svals.erase(svals.npos == nidx ? 0 : ++nidx); // trim windows /r if its there

        std::vector<std::string> vals = Utils::Split(svals, "\t"); // split on tabs
        if (vals.size() == 3)
        {
          kodi::addon::PVREDLEntry entry;
          entry.SetStart(static_cast<int64_t>(std::strtod(vals[0].c_str(), nullptr) * 1000)); // convert s to ms
          entry.SetEnd(static_cast<int64_t>(std::strtod(vals[1].c_str(), nullptr) * 1000));
          entry.SetType(PVR_EDL_TYPE(atoi(vals[2].c_str())));
          edl.emplace_back(entry);
        }
      }
      if (!edl.empty())
        kodi::Log(ADDON_LOG_DEBUG, "EDL data found.");
      else
        kodi::Log(ADDON_LOG_DEBUG, "No EDL data found.");
      return PVR_ERROR_NO_ERROR;
    }
    else
      kodi::Log(ADDON_LOG_DEBUG, "No EDL file found.");
  }
  return PVR_ERROR_FAILED;
}
