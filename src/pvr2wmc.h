/*
 *      Copyright (C) 2005-2015 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include <vector>
#include "p8-platform/util/StdString.h"
#include "client.h"
#include "Socket.h"

/* timer type ids */
#define TIMER_MANUAL_MIN			(PVR_TIMER_TYPE_NONE + 1)
#define TIMER_ONCE_MANUAL			(TIMER_MANUAL_MIN + 0)
#define TIMER_ONCE_EPG				(TIMER_MANUAL_MIN + 1)
#define TIMER_ONCE_KEYWORD			(TIMER_MANUAL_MIN + 2)
#define TIMER_ONCE_MANUAL_CHILD		(TIMER_MANUAL_MIN + 3)
#define TIMER_ONCE_EPG_CHILD		(TIMER_MANUAL_MIN + 4)
#define TIMER_ONCE_KEYWORD_CHILD	(TIMER_MANUAL_MIN + 5)
#define TIMER_MANUAL_MAX			(TIMER_MANUAL_MIN + 5)

#define TIMER_REPEATING_MIN			(TIMER_MANUAL_MAX + 1)
#define TIMER_REPEATING_MANUAL		(TIMER_REPEATING_MIN + 0)
#define TIMER_REPEATING_EPG			(TIMER_REPEATING_MIN + 1)
#define TIMER_REPEATING_KEYWORD		(TIMER_REPEATING_MIN + 2)
#define TIMER_REPEATING_MAX			(TIMER_REPEATING_MIN + 2)

typedef enum {
	WMC_PRIORITY_NORMAL = 0,
	WMC_PRIORITY_HIGH = 1,
	WMC_PRIORITY_LOW = 2
} wmc_priority_t;

typedef enum {
	WMC_SHOWTYPE_ANY = 0,
	WMC_SHOWTYPE_FIRSTRUNONLY = 1,
	WMC_SHOWTYPE_LIVEONLY = 2
} wmc_showtype_t;

typedef enum {
	WMC_LIFETIME_NOTSET = -4,
	WMC_LIFETIME_LATEST = -3,
	WMC_LIFETIME_WATCHED = -2,
	WMC_LIFETIME_ELIGIBLE = -1,
	WMC_LIFETIME_DELETED = 0,
	WMC_LIFETIME_ONEWEEK = 7
} wmc_lifetime_t;

typedef enum {
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

class Pvr2Wmc 
{
public:
	Pvr2Wmc(void);
	virtual ~Pvr2Wmc(void);

	virtual bool IsServerDown();
	virtual void UnLoading();
	const char *GetBackendVersion(void);
	PVR_ERROR GetTimerTypes ( PVR_TIMER_TYPE types[], int *size );
	virtual PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed);

	// channels
	virtual int GetChannelsAmount(void);
	virtual PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio);
	
	virtual int GetChannelGroupsAmount(void);
	virtual PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio);
	virtual PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group);

	// epg
	virtual PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd);
	
	// timers
	virtual PVR_ERROR GetTimers(ADDON_HANDLE handle);
	virtual PVR_ERROR AddTimer(const PVR_TIMER &timer);
	virtual PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete);
	virtual int GetTimersAmount(void);

	// recording files
	virtual PVR_ERROR GetRecordings(ADDON_HANDLE handle);
	PVR_ERROR DeleteRecording(const PVR_RECORDING &recording);
	PVR_ERROR RenameRecording(const PVR_RECORDING &recording);
	PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition);
	int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording);
	PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count);

	// recording streams
	bool OpenRecordedStream(const PVR_RECORDING &recording);
	virtual int GetRecordingsAmount(void);
	void UpdateRecordingTimer(int msec);

	// live tv
	bool OpenLiveStream(const PVR_CHANNEL &channel);
	bool CloseLiveStream(bool notifyServer = true);
	int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize);
	void PauseStream(bool bPaused);
	long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) ;
	long long PositionLiveStream(void) ;
	bool SwitchChannel(const PVR_CHANNEL &channel);
	long long LengthLiveStream(void);
	long long ActualFileSize(int count);
	PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus);

	// time shifting
	bool IsTimeShifting();
	time_t GetPlayingTime();
	time_t GetBufferTimeStart();
	time_t GetBufferTimeEnd();

	bool CheckErrorOnServer();
	void TriggerUpdates(std::vector<CStdString> results);
	void ExtractDriveSpace(std::vector<CStdString> results);

private:
	int _serverBuild;
	CStdString Timer2String(const PVR_TIMER &xTmr);
	CStdString SeriesTimer2String(const PVR_TIMER &xTmr);
	CStdString Channel2String(const PVR_CHANNEL &xTmr);

	Socket _socketClient;

	long long _diskTotal;
	long long _diskUsed;

	int _signalStatusCount;				// call backend for signal status every N calls (because XBMC calls every 1 second!)
	bool _discardSignalStatus;			// flag to discard signal status for channels where the backend had an error

	void* _streamFile;					// handle to a streamed file
	CStdString _streamFileName;			// the name of the stream file
	bool _lostStream;					// set to true if stream is lost
	
	bool _streamWTV;					// if true, stream wtv files
	long long _lastStreamSize;			// last value found for file stream
	bool _isStreamFileGrowing;			// true if server reports that a live/rec stream is still growing
	long long _readCnt;					// keep a count of the number of reads executed during playback

	int _initialStreamResetCnt;			// used to count how many times we reset the stream position (due to 2 pass demuxer)
	long long _initialStreamPosition;	// used to set an initial position (multiple clients watching the same live tv buffer)

	bool _insertDurationHeader;			// if true, insert a duration header for active Rec TS file
	CStdString _durationHeader;			// the header to insert (received from server)

	int _defaultPriority;
	int _defaultLiftetime;
	int _defaultLimit;
	int _defaultShowType;
};
