/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "Socket.h"
#include "addon.h"
#include "settings.h"

#include <chrono>
#include <kodi/Filesystem.h>
#include <kodi/addon-instance/PVR.h>

/* timer type ids */
#define TIMER_MANUAL_MIN (PVR_TIMER_TYPE_NONE + 1)
#define TIMER_ONCE_MANUAL (TIMER_MANUAL_MIN + 0)
#define TIMER_ONCE_EPG (TIMER_MANUAL_MIN + 1)
#define TIMER_ONCE_KEYWORD (TIMER_MANUAL_MIN + 2)
#define TIMER_ONCE_MANUAL_CHILD (TIMER_MANUAL_MIN + 3)
#define TIMER_ONCE_EPG_CHILD (TIMER_MANUAL_MIN + 4)
#define TIMER_ONCE_KEYWORD_CHILD (TIMER_MANUAL_MIN + 5)
#define TIMER_MANUAL_MAX (TIMER_MANUAL_MIN + 5)

#define TIMER_REPEATING_MIN (TIMER_MANUAL_MAX + 1)
#define TIMER_REPEATING_MANUAL (TIMER_REPEATING_MIN + 0)
#define TIMER_REPEATING_EPG (TIMER_REPEATING_MIN + 1)
#define TIMER_REPEATING_KEYWORD (TIMER_REPEATING_MIN + 2)
#define TIMER_REPEATING_MAX (TIMER_REPEATING_MIN + 2)

typedef enum
{
  WMC_PRIORITY_NORMAL = 0,
  WMC_PRIORITY_HIGH = 1,
  WMC_PRIORITY_LOW = 2
} wmc_priority_t;

typedef enum
{
  WMC_SHOWTYPE_ANY = 0,
  WMC_SHOWTYPE_FIRSTRUNONLY = 1,
  WMC_SHOWTYPE_LIVEONLY = 2
} wmc_showtype_t;

typedef enum
{
  WMC_LIFETIME_NOTSET = -4,
  WMC_LIFETIME_LATEST = -3,
  WMC_LIFETIME_WATCHED = -2,
  WMC_LIFETIME_ELIGIBLE = -1,
  WMC_LIFETIME_DELETED = 0,
  WMC_LIFETIME_ONEWEEK = 7
} wmc_lifetime_t;

typedef enum
{
  WMC_LIMIT_ASMANY = -1,
  WMC_LIMIT_1 = 1,
  WMC_LIMIT_2 = 2,
  WMC_LIMIT_3 = 3,
  WMC_LIMIT_4 = 4,
  WMC_LIMIT_5 = 5,
  WMC_LIMIT_6 = 6,
  WMC_LIMIT_7 = 7,
  WMC_LIMIT_10 = 10
} wmc_recordinglimit_t;

enum backend_status
{
  BACKEND_UNKNOWN,
  BACKEND_DOWN,
  BACKEND_UP
};

class ATTR_DLL_LOCAL Pvr2Wmc : public kodi::addon::CInstancePVRClient
{
public:
  Pvr2Wmc(CPvr2WmcAddon& addon, const kodi::addon::IInstanceInfo& instance);
  ~Pvr2Wmc() override = default;

  bool IsServerDown();
  void UnLoading();

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetBackendHostname(std::string& hostname) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;

  // channels
  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;

  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                   kodi::addon::PVRChannelGroupMembersResultSet& results) override;

  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;

  // epg
  PVR_ERROR GetEPGForChannel(int channelUid,
                             time_t start,
                             time_t end,
                             kodi::addon::PVREPGTagsResultSet& results) override;

  // timers
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;

  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR GetTimersAmount(int& amount) override;

  // recording files
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR RenameRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording,
                                           int lastplayedposition) override;
  PVR_ERROR GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording,
                                           int& position) override;
  PVR_ERROR SetRecordingPlayCount(const kodi::addon::PVRRecording& recording, int count) override;

  // comm skip
  PVR_ERROR GetRecordingEdl(const kodi::addon::PVRRecording& recording,
                            std::vector<kodi::addon::PVREDLEntry>& edl) override;

  // recording streams
  bool OpenRecordedStream(const kodi::addon::PVRRecording& recording, int64_t& streamId) override;
  void CloseRecordedStream(int64_t streamId) override { CloseStream(); }
  int ReadRecordedStream(int64_t streamId, unsigned char* buffer, unsigned int size) override
  {
    return ReadStream(buffer, size);
  }
  int64_t SeekRecordedStream(int64_t streamId, int64_t position, int whence) override
  {
    return SeekStream(position, whence);
  }
  int64_t LengthRecordedStream(int64_t streamId) override { return LengthStream(); }

  // live tv
  bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override;
  void CloseLiveStream() override { CloseStream(); }
  int ReadLiveStream(unsigned char* buffer, unsigned int size) override
  {
    return ReadStream(buffer, size);
  }
  int64_t SeekLiveStream(int64_t position, int whence) override
  {
    return SeekStream(position, whence);
  }
  int64_t LengthLiveStream() override { return LengthStream(); }

  // time shifting
  PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& times) override;
  bool IsRealTimeStream() override { return !_bRecordingPlayback; }
  bool CanPauseStream() override { return true; }
  bool CanSeekStream() override { return true; }

  const CSettings& GetSettings() const { return _addon.GetSettings(); }

  void SetBackendStatus(backend_status status) { _BackendOnline = status; }
  backend_status GetBackendStatus() const { return _BackendOnline; }

private:
  int _serverBuild;
  std::string Timer2String(const kodi::addon::PVRTimer& xTmr);
  std::string Channel2String(const kodi::addon::PVRChannel& xTmr);

  bool CloseStream(bool notifyServer = true);
  int ReadStream(unsigned char* pBuffer, unsigned int iBufferSize);
  int64_t SeekStream(int64_t iPosition, int iWhence /* = SEEK_SET */);
  int64_t PositionStream();
  int64_t LengthStream();
  int64_t ActualFileSize(int count);

  bool CheckErrorOnServer();
  void TriggerUpdates(std::vector<std::string> results);
  void ExtractDriveSpace(std::vector<std::string> results);

  Socket _socketClient;

  uint64_t _diskTotal = 0;
  uint64_t _diskUsed = 0;

  int _signalStatusCount =
      0; // call backend for signal status every N calls (because XBMC calls every 1 second!)
  bool _discardSignalStatus =
      false; // flag to discard signal status for channels where the backend had an error

  kodi::vfs::CFile _streamFile; // handle to a streamed file
  std::string _streamFileName; // the name of the stream file
  bool _lostStream = false; // set to true if stream is lost
  bool _bRecordingPlayback = false;

  // save the last pos of buffer pointers so that we can filter how often swmc is quered for buff
  // data
  time_t _savBuffStart;
  int64_t _savBuffEnd;

  static long _buffTimesCnt; // filter how often we do buffer status reads
  static long _buffTimeFILTER;

  bool _streamWTV = true; // if true, stream wtv files
  int64_t _lastStreamSize = 0; // last value found for file stream
  bool _isStreamFileGrowing =
      false; // true if server reports that a live/rec stream is still growing
  int64_t _readCnt = 0; // keep a count of the number of reads executed during playback

  int _initialStreamResetCnt =
      0; // used to count how many times we reset the stream position (due to 2 pass demuxer)
  int64_t _initialStreamPosition =
      0; // used to set an initial position (multiple clients watching the same live tv buffer)

  bool _insertDurationHeader = false; // if true, insert a duration header for active Rec TS file
  std::string _durationHeader; // the header to insert (received from server)

  int _defaultPriority = WMC_PRIORITY_NORMAL;
  int _defaultLiftetime = WMC_LIFETIME_ELIGIBLE;
  int _defaultLimit = WMC_LIMIT_ASMANY;
  int _defaultShowType = WMC_SHOWTYPE_ANY;

  std::chrono::high_resolution_clock::time_point
      _lastRecordingUpdateTime; // the time of the last recording display update

  backend_status _BackendOnline = BACKEND_DOWN;

  CPvr2WmcAddon& _addon;
};
