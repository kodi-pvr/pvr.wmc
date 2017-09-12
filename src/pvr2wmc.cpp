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

#include "util/XMLUtils.h"
#include "pvr2wmc.h"
#include "utilities.h"
#include "p8-platform/util/timeutils.h"

#include <memory>

using namespace std;
using namespace ADDON;
using namespace P8PLATFORM;

#define null NULL

#define STRCPY(dest, src) strncpy(dest, src, sizeof(dest)-1); 
#define FOREACH(ss, vv) for(std::vector<std::string>::iterator ss = vv.begin(); ss != vv.end(); ++ss)

#define FAKE_TS_LENGTH 2000000			// a fake file length for give to xbmc (used to insert duration headers)

int64_t _lastRecordingUpdateTime;		// the time of the last recording display update

int _buffTimesCnt;						// filter how often we do buffer status reads
int _buffTimeFILTER;

Pvr2Wmc::Pvr2Wmc(void)
{
	_socketClient.SetServerName(g_strServerName);
	_socketClient.SetClientName(g_strClientName);
	_socketClient.SetServerPort(g_port);

	_diskTotal = 0;
	_diskUsed = 0;

	_signalStatusCount = 0;			// for signal status display
	_discardSignalStatus = false;

	_streamFile = 0;				// handle to a streamed file
	_streamFileName = "";
	_readCnt = 0;

	_initialStreamResetCnt = 0;		// used to count how many times we reset the stream position (due to 2 pass demuxer)
	_initialStreamPosition = 0;		// used to set an initial position (multiple clients watching the same live tv buffer)
	
	_insertDurationHeader = false;	// if true, insert a duration header for active Rec TS file
	_durationHeader = "";			// the header to insert (received from server)
	
	_lastRecordingUpdateTime = 0;	// time of last recording display update
	_lostStream = false;			// set to true if stream is lost
	_lastStreamSize = 0;			// the last value found for the stream file size
	_isStreamFileGrowing = false;	// true if stream file is growing (server determines)
	_streamWTV = true;				// by default, assume we are streaming Wtv files (not ts files)

	_defaultPriority = WMC_PRIORITY_NORMAL;
	_defaultLiftetime = WMC_LIFETIME_ELIGIBLE;
	_defaultLimit = WMC_LIMIT_ASMANY;
	_defaultShowType = WMC_SHOWTYPE_ANY;
}

Pvr2Wmc::~Pvr2Wmc(void)
{
}

bool Pvr2Wmc::IsServerDown()
{
	std::string request;
	request = string_format("GetServiceStatus|%s|%s", PVRWMC_GetClientVersion().c_str(), g_clientOS.c_str());
	_socketClient.SetTimeOut(10);											// set a timout interval for checking if server is down
	vector<std::string> results = _socketClient.GetVector(request, true);	// get serverstatus
	bool isServerDown = (results[0] != "True");								// true if server is down

	// GetServiceStatus may return any updates requested by server
	if (!isServerDown && results.size() > 1)								// if server is not down and there are additional fields
	{
		ExtractDriveSpace(results);											// get drive space total/used from backend response
		TriggerUpdates(results);											// send update array to trigger updates requested by server
	}
	return isServerDown;
}

void Pvr2Wmc::UnLoading()
{
	_socketClient.GetBool("ClientGoingDown", true, false);			// returns true if server is up
}

const char *Pvr2Wmc::GetBackendVersion(void)
{
	if (!IsServerDown())
	{
		static std::string strVersion = "0.0";

		// Send client's time (in UTC) to backend
		time_t now = time(NULL);
		char datestr[32];
		strftime(datestr, 32, "%Y-%m-%d %H:%M:%S", gmtime(&now));

		// Also send this client's setting for backend servername (so server knows how it is being accessed)
		std::string request;
		request = string_format("GetServerVersion|%s|%s", datestr, g_strServerName.c_str());
		vector<std::string> results = _socketClient.GetVector(request, true);
		if (results.size() > 0)
		{
			strVersion = std::string(results[0]);
		}
		if (results.size() > 1)
		{
			_serverBuild = atoi(results[1].c_str());			// get server build number for feature checking
		}
		// check if recorded tv folder is accessible from client
		if (results.size() > 2 && results[2] != "")		// if server sends empty string, skip check
		{
			if (!XBMC->DirectoryExists(results[2].c_str()))
			{
				XBMC->Log(LOG_ERROR, "Recorded tv '%s' does not exist", results[2].c_str());
				std::string infoStr = XBMC->GetLocalizedString(30017);		
				XBMC->QueueNotification(QUEUE_ERROR, infoStr.c_str());
			}
			else if (!XBMC->CanOpenDirectory(results[2].c_str()))
			{
				XBMC->Log(LOG_ERROR, "Recorded tv '%s' count not be opened", results[2].c_str());
				std::string infoStr = XBMC->GetLocalizedString(30018);		
				XBMC->QueueNotification(QUEUE_ERROR, infoStr.c_str());
			}
		}
		// check if server returned it's MAC address
		if (results.size() > 3 && results[3] != "" && results[3] != g_strServerMAC)
		{
			XBMC->Log(LOG_INFO, "Setting ServerWMC Server MAC Address to '%s'", results[3].c_str());
			g_strServerMAC = results[3];
		
			// Attempt to save MAC address to custom addon data
			WriteFileContents(g_AddonDataCustom, g_strServerMAC);
		}
		
		return strVersion.c_str();	// return server version to caller
	}
	return "Not accessible";	// server version check failed
}

namespace
{
struct TimerType : PVR_TIMER_TYPE
{
  TimerType(unsigned int id,
			unsigned int attributes,
			const std::string &description,
			const std::vector< std::pair<int, std::string> > &priorityValues,
			int priorityDefault,
			const std::vector< std::pair<int, std::string> > &lifetimeValues,
			int lifetimeDefault,
			const std::vector< std::pair<int, std::string> > &maxRecordingsValues,
			int maxRecordingsDefault,
			const std::vector< std::pair<int, std::string> > &dupEpisodesValues,
			int dupEpisodesDefault
			)
  {
	memset(this, 0, sizeof(PVR_TIMER_TYPE));

	iId								 = id;
	iAttributes						 = attributes;
	iPrioritiesSize					 = priorityValues.size();
	iPrioritiesDefault				 = priorityDefault;
	iLifetimesSize					 = lifetimeValues.size();
	iLifetimesDefault				 = lifetimeDefault;
	iMaxRecordingsSize				 = maxRecordingsValues.size();
	iMaxRecordingsDefault			 = maxRecordingsDefault;
	iPreventDuplicateEpisodesSize	 = dupEpisodesValues.size();
	iPreventDuplicateEpisodesDefault = dupEpisodesDefault;
	strncpy(strDescription, description.c_str(), sizeof(strDescription) - 1);

	int i = 0;
	for (auto it = priorityValues.begin(); it != priorityValues.end(); ++it, ++i)
	{
		priorities[i].iValue = it->first;
		strncpy(priorities[i].strDescription, it->second.c_str(), sizeof(priorities[i].strDescription) - 1);
	}

	i = 0;
	for (auto it = lifetimeValues.begin(); it != lifetimeValues.end(); ++it, ++i)
	{
		lifetimes[i].iValue = it->first;
		strncpy(lifetimes[i].strDescription, it->second.c_str(), sizeof(lifetimes[i].strDescription) - 1);
	}

	i = 0;
	for (auto it = maxRecordingsValues.begin(); it != maxRecordingsValues.end(); ++it, ++i)
	{
		maxRecordings[i].iValue = it->first;
		strncpy(maxRecordings[i].strDescription, it->second.c_str(), sizeof(maxRecordings[i].strDescription) - 1);
	}

	i = 0;
	for (auto it = dupEpisodesValues.begin(); it != dupEpisodesValues.end(); ++it, ++i)
	{
		preventDuplicateEpisodes[i].iValue = it->first;
		strncpy(preventDuplicateEpisodes[i].strDescription, it->second.c_str(), sizeof(preventDuplicateEpisodes[i].strDescription) - 1);
	}
  }
};

} // unnamed namespace

PVR_ERROR Pvr2Wmc::GetTimerTypes ( PVR_TIMER_TYPE types[], int *size )
{
	/* PVR_Timer.iPriority values and presentation.*/
	static std::vector< std::pair<int, std::string> > priorityValues;
	if (priorityValues.size() == 0)
	{
		priorityValues.push_back(std::make_pair(WMC_PRIORITY_NORMAL,		XBMC->GetLocalizedString(30140)));
		priorityValues.push_back(std::make_pair(WMC_PRIORITY_HIGH,			XBMC->GetLocalizedString(30141)));
		priorityValues.push_back(std::make_pair(WMC_PRIORITY_LOW,			XBMC->GetLocalizedString(30142)));
	}

	/* PVR_Timer.iLifeTime values and presentation.*/
	static std::vector< std::pair<int, std::string> > lifetimeValues;
	if (lifetimeValues.size() == 0)
	{
		lifetimeValues.push_back(std::make_pair(WMC_LIFETIME_NOTSET,		XBMC->GetLocalizedString(30160)));
		lifetimeValues.push_back(std::make_pair(WMC_LIFETIME_LATEST,		XBMC->GetLocalizedString(30161)));
		lifetimeValues.push_back(std::make_pair(WMC_LIFETIME_WATCHED,		XBMC->GetLocalizedString(30162)));
		lifetimeValues.push_back(std::make_pair(WMC_LIFETIME_ELIGIBLE,		XBMC->GetLocalizedString(30163)));
		lifetimeValues.push_back(std::make_pair(WMC_LIFETIME_DELETED,		XBMC->GetLocalizedString(30164)));
		lifetimeValues.push_back(std::make_pair(WMC_LIFETIME_ONEWEEK,		XBMC->GetLocalizedString(30165)));
	}

	/* PVR_Timer.iMaxRecordings values and presentation. */
	static std::vector< std::pair<int, std::string> > recordingLimitValues;
	if (recordingLimitValues.size() == 0)
	{
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_ASMANY,		XBMC->GetLocalizedString(30170)));
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_1,			XBMC->GetLocalizedString(30171)));
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_2,			XBMC->GetLocalizedString(30172)));
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_3,			XBMC->GetLocalizedString(30173)));
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_4,			XBMC->GetLocalizedString(30174)));
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_5,			XBMC->GetLocalizedString(30175)));
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_6,			XBMC->GetLocalizedString(30176)));
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_7,			XBMC->GetLocalizedString(30177)));
		recordingLimitValues.push_back(std::make_pair(WMC_LIMIT_10,			XBMC->GetLocalizedString(30178)));
	}

	/* PVR_Timer.iPreventDuplicateEpisodes values and presentation.*/
	static std::vector< std::pair<int, std::string> > showTypeValues;
	if (showTypeValues.size() == 0)
	{
		showTypeValues.push_back(std::make_pair(WMC_SHOWTYPE_FIRSTRUNONLY,	XBMC->GetLocalizedString(30150)));
		showTypeValues.push_back(std::make_pair(WMC_SHOWTYPE_ANY,			XBMC->GetLocalizedString(30151)));
		showTypeValues.push_back(std::make_pair(WMC_SHOWTYPE_LIVEONLY,		XBMC->GetLocalizedString(30152)));
	}

	static std::vector< std::pair<int, std::string> > emptyList;

	static const unsigned int TIMER_MANUAL_ATTRIBS
	  =	PVR_TIMER_TYPE_IS_MANUAL							|
		PVR_TIMER_TYPE_SUPPORTS_CHANNELS					|
		PVR_TIMER_TYPE_SUPPORTS_START_TIME					|
		PVR_TIMER_TYPE_SUPPORTS_END_TIME					|
		PVR_TIMER_TYPE_SUPPORTS_PRIORITY					|
		PVR_TIMER_TYPE_SUPPORTS_LIFETIME;

	static const unsigned int TIMER_EPG_ATTRIBS
	  =	PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE			|
		PVR_TIMER_TYPE_SUPPORTS_CHANNELS					|
		PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN			|
		PVR_TIMER_TYPE_SUPPORTS_PRIORITY					|
		PVR_TIMER_TYPE_SUPPORTS_LIFETIME;

	static const unsigned int TIMER_KEYWORD_ATTRIBS
	  =	PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH			|
		PVR_TIMER_TYPE_SUPPORTS_CHANNELS					|
		PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH				|
		PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN			|
		PVR_TIMER_TYPE_SUPPORTS_PRIORITY					|
		PVR_TIMER_TYPE_SUPPORTS_LIFETIME;

	static const unsigned int TIMER_REPEATING_MANUAL_ATTRIBS
	  = PVR_TIMER_TYPE_IS_REPEATING							|
		PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS					|
		PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

	static const unsigned int TIMER_REPEATING_EPG_ATTRIBS
	  =	PVR_TIMER_TYPE_IS_REPEATING							|
		PVR_TIMER_TYPE_REQUIRES_EPG_SERIES_ON_CREATE		|
		PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME				|
		PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS					|
		PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES	|
		PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

	static const unsigned int TIMER_REPEATING_KEYWORD_ATTRIBS
	  =	PVR_TIMER_TYPE_IS_REPEATING							|
		PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES	|
		PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

	static const unsigned int TIMER_CHILD_ATTRIBUTES
	  = PVR_TIMER_TYPE_SUPPORTS_START_TIME					|
		PVR_TIMER_TYPE_SUPPORTS_END_TIME					|
		PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES;

	/* Timer types definition.*/
	static std::vector< std::unique_ptr< TimerType > > timerTypes;
	if (timerTypes.size() == 0)
	{
	timerTypes.push_back(
		/* One-shot manual (time and channel based) */
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_ONCE_MANUAL,
		/* Attributes. */
		TIMER_MANUAL_ATTRIBS,
		/* Description. */
		XBMC->GetLocalizedString(30131), // "One time (manual)",
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));

	timerTypes.push_back(
		/* One-shot epg based */
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_ONCE_EPG,
		/* Attributes. */
		TIMER_EPG_ATTRIBS,
		/* Description. */
		XBMC->GetLocalizedString(30132), // "One time (guide)",
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));

	timerTypes.push_back(
		/* One shot Keyword based */
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_ONCE_KEYWORD,
		/* Attributes. */
		TIMER_KEYWORD_ATTRIBS,
		/* Description. */
		XBMC->GetLocalizedString(30133), // "One time (wishlist)"
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));

	timerTypes.push_back(
		/* Read-only one-shot for timers generated by timerec */
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_ONCE_MANUAL_CHILD,
		/* Attributes. */
		TIMER_MANUAL_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
		/* Description. */
		XBMC->GetLocalizedString(30130), // "Created by Repeating Timer"
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));

	timerTypes.push_back(
		/* Read-only one-shot for timers generated by autorec */
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_ONCE_EPG_CHILD,
		/* Attributes. */
		TIMER_EPG_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
		/* Description. */
		XBMC->GetLocalizedString(30130), // "Created by Repeating Timer"
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));

	timerTypes.push_back(
		/* One shot Keyword based */
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_ONCE_KEYWORD_CHILD,
		/* Attributes. */
		TIMER_KEYWORD_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
		/* Description. */
		XBMC->GetLocalizedString(30130), // "Created by Repeating Timer"
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));

	timerTypes.push_back(
		/* Repeating manual (time and channel based) Parent */
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_REPEATING_MANUAL,
		/* Attributes. */
		TIMER_MANUAL_ATTRIBS | TIMER_REPEATING_MANUAL_ATTRIBS,
		/* Description. */
		XBMC->GetLocalizedString(30134), // "Repeating (manual)"
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));

	timerTypes.push_back(
		/* Repeating epg based Parent*/
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_REPEATING_EPG,
		/* Attributes. */
		TIMER_EPG_ATTRIBS | TIMER_REPEATING_EPG_ATTRIBS,
		/* Description. */
		XBMC->GetLocalizedString(30135), // "Repeating (guide)"
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));

	timerTypes.push_back(
		/* Repeating Keyword (Generic) based */
		std::unique_ptr<TimerType>(new TimerType(
		/* Type id. */
		TIMER_REPEATING_KEYWORD,
		/* Attributes. */
		TIMER_KEYWORD_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
		/* Description. */
		XBMC->GetLocalizedString(30136), // "Repeating (wishlist)"
		/* Values definitions for attributes. */
		priorityValues, _defaultPriority,
		lifetimeValues, _defaultLiftetime,
		recordingLimitValues, _defaultLimit,
		showTypeValues, _defaultShowType)));
	}

	/* Copy data to target array. */
	int i = 0;
	for (auto it = timerTypes.begin(); it != timerTypes.end(); ++it, ++i)
		types[i] = **it;

	*size = timerTypes.size();
	return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetDriveSpace(long long *iTotal, long long *iUsed)
{
	*iTotal = _diskTotal;
	*iUsed = _diskUsed;

	return PVR_ERROR_NO_ERROR;
}

int Pvr2Wmc::GetChannelsAmount(void)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	return _socketClient.GetInt("GetChannelCount", true);
}

// test for returned error vector from server, handle accompanying messages if any
bool isServerError(vector<std::string> results)
{
	if (results[0] == "error")
	{
		if (results.size() > 1 && results[1].length() != 0)
		{
			XBMC->Log(LOG_ERROR, results[1].c_str());	// log more info on error
		}
		if (results.size() > 2 != 0)
		{
			int errorID = atoi(results[2].c_str());
			if (errorID != 0)
			{
				std::string errStr = XBMC->GetLocalizedString(errorID);
				XBMC->QueueNotification(QUEUE_ERROR, errStr.c_str());
			}
		}
		return true;
	}
	else 
		return false;
}

// look at result vector from server and perform any updates requested
void Pvr2Wmc::TriggerUpdates(vector<std::string> results)
{
	FOREACH(response, results)
	{
		vector<std::string> v = split(*response, "|");				// split to unpack string

		if (v.size() < 1)
		{
			XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for Triggers/Message");
			return;
		}

		if (v[0] == "updateTimers")
			PVR->TriggerTimerUpdate();
		else if (v[0] == "updateRecordings")
			PVR->TriggerRecordingUpdate();
		else if (v[0] == "updateChannels")
			PVR->TriggerChannelUpdate();
		else if (v[0] == "updateChannelGroups")
			PVR->TriggerChannelGroupsUpdate();
		else if (v[0] == "updateEPGForChannel")
		{
			if (v.size() > 1)
			{
				unsigned int channelUid = strtoul(v[1].c_str(), 0, 10);
				PVR->TriggerEpgUpdate(channelUid);
			}
		}
		else if (v[0] == "message")
		{
			if (v.size() < 4)
			{
				XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for Message");
				return;
			}

			XBMC->Log(LOG_INFO, "Received message from backend: %s", response->c_str());
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
			infoStr = XBMC->GetLocalizedString(stringId);

			// Use text from backend if stringID not found
			if (infoStr == "")
			{
				infoStr = v[3];
			}

			// Send XBMC Notification (support up to 4 parameter replaced arguments from the backend)
			if (v.size() == 4)
			{
				XBMC->QueueNotification((ADDON::queue_msg)level, infoStr.c_str());
			}
			else if (v.size() == 5)
			{
				XBMC->QueueNotification((ADDON::queue_msg)level, infoStr.c_str(), v[4].c_str());
			}
			else if (v.size() == 6)
			{
				XBMC->QueueNotification((ADDON::queue_msg)level, infoStr.c_str(), v[4].c_str(), v[5].c_str());
			}
			else if (v.size() == 7)
			{
				XBMC->QueueNotification((ADDON::queue_msg)level, infoStr.c_str(), v[4].c_str(), v[5].c_str(), v[6].c_str());
			}
			else
			{
				XBMC->QueueNotification((ADDON::queue_msg)level, infoStr.c_str(), v[4].c_str(), v[5].c_str(), v[6].c_str(), v[7].c_str());
			}
		}
	}
}

void Pvr2Wmc::ExtractDriveSpace(vector<std::string> results)
{
	FOREACH(response, results)
	{
		vector<std::string> v = split(*response, "|");				// split to unpack string

		if (v.size() < 1)
		{
			continue;
		}

		if (v[0] == "driveSpace")
		{
			if (v.size() > 1)
			{
				
				long long totalSpace = strtoll(v[1].c_str(), 0, 10);
				long long freeSpace = strtoll(v[2].c_str(), 0, 10);
				long long usedSpace = strtoll(v[3].c_str(), 0, 10);
				_diskTotal = totalSpace / 1024;
				_diskUsed = usedSpace / 1024;
			}
		}
	}
}
// xbmc call: get all channels for either tv or radio
PVR_ERROR Pvr2Wmc::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string request;
	request = string_format("GetChannels|%s", bRadio ? "True" : "False");
	vector<std::string> results = _socketClient.GetVector(request, true);
	
	FOREACH(response, results)
	{ 
		PVR_CHANNEL xChannel;

		memset(&xChannel, 0, sizeof(PVR_CHANNEL));							// set all mem to zero
		vector<std::string> v = StringUtils::Split(*response, "|");
		// packing: id, bradio, c.OriginalNumber, c.CallSign, c.IsEncrypted, imageStr, c.IsBlocked

		if (v.size() < 9)
		{
			XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for channel data");
			continue;
		}

		// Populate Channel (and optionally subchannel if one was provided)
		vector<std::string> c = split(v[7], ".");
		if (c.size() > 1)
		{
			xChannel.iChannelNumber = atoi(c[0].c_str());
			xChannel.iSubChannelNumber = atoi(c[1].c_str());
		}
		else
		{
			xChannel.iChannelNumber = atoi(v[2].c_str());
		}

		xChannel.iUniqueId = strtoul(v[0].c_str(), 0, 10);					// convert to unsigned int
		xChannel.bIsRadio = Str2Bool(v[1]);
		STRCPY(xChannel.strChannelName, v[3].c_str());
		xChannel.iEncryptionSystem = Str2Bool(v[4]);
		if (v[5].compare("NULL") != 0)										// if icon path is not null
			STRCPY(xChannel.strIconPath, v[5].c_str());
		xChannel.bIsHidden = Str2Bool(v[6]);

		PVR->TransferChannelEntry(handle, &xChannel);
	}
	return PVR_ERROR_NO_ERROR;
}

int Pvr2Wmc::GetChannelGroupsAmount(void)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	return _socketClient.GetInt("GetChannelGroupCount", true);
}

PVR_ERROR Pvr2Wmc::GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string request;
	request = string_format("GetChannelGroups|%s", bRadio ? "True" : "False");
	vector<std::string> results = _socketClient.GetVector(request, true);

	FOREACH(response, results)
	{ 
		PVR_CHANNEL_GROUP xGroup;
		memset(&xGroup, 0, sizeof(PVR_CHANNEL_GROUP));

		vector<std::string> v = split(*response, "|");

		if (v.size() < 1)
		{
			XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for channel group data");
			continue;
		}

		xGroup.bIsRadio = bRadio;
		strncpy(xGroup.strGroupName, v[0].c_str(), sizeof(xGroup.strGroupName) - 1);

		// PVR API 1.9.6 adds Position field
		if (v.size() >= 2)
		{
			xGroup.iPosition = atoi(v[1].c_str());
		}

		PVR->TransferChannelGroup(handle, &xGroup);
	}

	return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string request;
	request = string_format("GetChannelGroupMembers|%s|%s", group.bIsRadio ? "True" : "False", group.strGroupName);
	vector<std::string> results = _socketClient.GetVector(request, true);

	FOREACH(response, results)
	{ 
		PVR_CHANNEL_GROUP_MEMBER xGroupMember;
		memset(&xGroupMember, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));

		vector<std::string> v = split(*response, "|");

		if (v.size() < 2)
		{
			XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for channel group member data");
			continue;
		}

		strncpy(xGroupMember.strGroupName, group.strGroupName, sizeof(xGroupMember.strGroupName) - 1);
		xGroupMember.iChannelUniqueId = strtoul(v[0].c_str(), 0, 10);					// convert to unsigned int
		xGroupMember.iChannelNumber =  atoi(v[1].c_str());

		PVR->TransferChannelGroupMember(handle, &xGroupMember);
	}

	return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string request;
	request = string_format("GetEntries|%u|%lld|%lld", channel.iUniqueId, (long long)iStart, (long long)iEnd);			// build the request string 

	vector<std::string> results = _socketClient.GetVector(request, true);			// get entries from server
	
	FOREACH(response, results)
	{ 
		EPG_TAG xEpg;
		memset(&xEpg, 0, sizeof(EPG_TAG));											// set all mem to zero
		vector<std::string> v = split(*response, "|");								// split to unpack string

		if (v.size() < 16)
		{
			XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for epg data");
			continue;
		}

		//	e.Id, e.Program.Title, c.OriginalNumber, start_t, end_t,   
		//	e.Program.ShortDescription, e.Program.Description,
		//	origAirDate, e.TVRating, e.Program.StarRating,
		//	e.Program.SeasonNumber, e.Program.EpisodeNumber,
		//	e.Program.EpisodeTitle
		//	(MB3 fields) channelID, audioFormat, GenreString, ProgramType
		//	actors, directors, writers, year, movieID
		xEpg.iUniqueChannelId = channel.iUniqueId;			// assign unique channel ID
		xEpg.iUniqueBroadcastId = atoi(v[0].c_str());		// entry ID
		xEpg.strTitle = v[1].c_str();						// entry title
		xEpg.startTime = atol(v[3].c_str());				// start time
		xEpg.endTime = atol(v[4].c_str());					// end time
		xEpg.strPlotOutline = v[5].c_str();					// short plot description (currently using episode name, if there is one)
		xEpg.strPlot = v[6].c_str();						// long plot description
		xEpg.firstAired = atol(v[7].c_str());				// orig air date
		xEpg.iParentalRating = atoi(v[8].c_str());			// tv rating
		xEpg.iStarRating = atoi(v[9].c_str());				// star rating
		xEpg.iSeriesNumber = atoi(v[10].c_str());			// season (?) number
		xEpg.iEpisodeNumber = atoi(v[11].c_str());			// episode number
		xEpg.iGenreType = atoi(v[12].c_str());				// season (?) number
		xEpg.iGenreSubType = atoi(v[13].c_str());			// general genre type
		xEpg.strIconPath = v[14].c_str();					// the icon url
		xEpg.strEpisodeName = v[15].c_str();				// the episode name
		xEpg.strGenreDescription = "";

		// Kodi PVR API 1.9.6 adds new EPG fields
		if (v.size() >= 25)
		{
			xEpg.strCast = v[20].c_str();
			xEpg.strDirector = v[21].c_str();
			xEpg.strWriter = v[22].c_str();
			xEpg.iYear = atoi(v[23].c_str());
			xEpg.strIMDBNumber = v[24].c_str();
		}

		// Kodi PVR API 4.1.0 adds new EPG iFlags field
		if (v.size() >= 26)
		{
			if (Str2Bool(v[25].c_str()))
			{
				xEpg.iFlags |= EPG_TAG_FLAG_IS_SERIES;
			}
		}

		PVR->TransferEpgEntry(handle, &xEpg);

	}
	return PVR_ERROR_NO_ERROR;
}


// timer functions -------------------------------------------------------------
int Pvr2Wmc::GetTimersAmount(void)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	return _socketClient.GetInt("GetTimerCount", true);
}

PVR_ERROR Pvr2Wmc::AddTimer(const PVR_TIMER &xTmr)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	// Send request to ServerWMC
	std::string command = "";
	command = "SetTimerKodi" + Timer2String(xTmr);	// convert timer to string

	vector<std::string> results = _socketClient.GetVector(command, false);	// get results from server

	PVR->TriggerTimerUpdate();							// update timers regardless of whether there is an error

	if (isServerError(results))
	{
		return PVR_ERROR_SERVER_ERROR;
	} 
	else 
	{
		XBMC->Log(LOG_DEBUG, "recording added for timer '%s', with rec state %s", xTmr.strTitle, results[0].c_str());

		if (results.size() > 1)											// if there is extra results sent from server...
		{
			FOREACH(result, results)
			{
				vector<std::string> splitResult = split(*result, "|");	// split to unpack extra info on each result
				std::string infoStr;

				if (splitResult[0] == "recordingNow")					// recording is active now
				{
					XBMC->Log(LOG_DEBUG, "timer recording is in progress");
				}
				else if (splitResult[0] == "recordingNowTimedOut")		// swmc timed out waiting for the recording to start
				{
					XBMC->Log(LOG_DEBUG, "server timed out waiting for in-progress recording to start");
				}
				else if (splitResult[0] == "recordingChannel")			// service picked a different channel for timer
				{
					XBMC->Log(LOG_DEBUG, "timer channel changed by wmc to '%s'", splitResult[1].c_str());
					// build info string and notify user of channel change
					infoStr = XBMC->GetLocalizedString(30009) + splitResult[1];		
					XBMC->QueueNotification(QUEUE_WARNING, infoStr.c_str());
				}
				else if (splitResult[0] == "recordingTime")				// service picked a different start time for timer
				{
					XBMC->Log(LOG_DEBUG, "timer start time changed by wmc to '%s'", splitResult[1].c_str());
					// build info string and notify user of time change
					infoStr = XBMC->GetLocalizedString(30010) + splitResult[1];
					XBMC->QueueNotification(QUEUE_WARNING, infoStr.c_str());
				}
				else if (splitResult[0] == "increasedEndTime")			// end time has been increased on an instant record
				{
					XBMC->Log(LOG_DEBUG, "instant record end time increased by '%s' minutes", splitResult[1].c_str());
					// build info string and notify user of time increase
					infoStr = XBMC->GetLocalizedString(30013) + splitResult[1] + " min";
					XBMC->QueueNotification(QUEUE_INFO, infoStr.c_str());
				}
			}
		}

		return PVR_ERROR_NO_ERROR;
	}
}

std::string Pvr2Wmc::Timer2String(const PVR_TIMER &xTmr)
{
	std::string tStr;

	bool bRepeating = xTmr.iTimerType >= TIMER_REPEATING_MIN && xTmr.iTimerType <= TIMER_REPEATING_MAX;
	bool bKeyword = xTmr.iTimerType == TIMER_REPEATING_KEYWORD || xTmr.iTimerType == TIMER_ONCE_KEYWORD || xTmr.iTimerType == TIMER_ONCE_KEYWORD_CHILD;
	bool bManual = xTmr.iTimerType == TIMER_ONCE_MANUAL || xTmr.iTimerType == TIMER_ONCE_MANUAL_CHILD || xTmr.iTimerType == TIMER_REPEATING_MANUAL;

	//was ("|%d|%d|%d|%d|%d|%s|%d|%d|%d|%d|%d",
	tStr = string_format("|%u|%d|%lld|%lld|%d|%s|%d|%u|%u|%d|%u",
		xTmr.iClientIndex, xTmr.iClientChannelUid, (long long)xTmr.startTime, (long long)xTmr.endTime, PVR_TIMER_STATE_NEW,		// 0-4
		xTmr.strTitle, xTmr.iPriority,  xTmr.iMarginStart, xTmr.iMarginEnd, bRepeating,						// 5-9
		xTmr.iEpgUid);																						// 10

	// Append extra fields from Kodi 16
	std::string extra;
	//was ("|%d|%d|%d|%d|%d|%d|%s|%d|%d",
	extra = string_format("|%u|%d|%u|%d|%d|%d|%s|%d|%d",
		xTmr.iPreventDuplicateEpisodes, xTmr.bStartAnyTime, xTmr.iWeekdays, // 11-13 param
		xTmr.iLifetime, bKeyword, xTmr.bFullTextEpgSearch, xTmr.strEpgSearchString, xTmr.iMaxRecordings, bManual); // 14-19
	tStr.append(extra);

	return tStr;
}

PVR_ERROR Pvr2Wmc::DeleteTimer(const PVR_TIMER &xTmr, bool bForceDelete)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	bool bRepeating = xTmr.iTimerType >= TIMER_REPEATING_MIN && xTmr.iTimerType <= TIMER_REPEATING_MAX;

	std::string command = "DeleteTimerKodi";
	command = string_format("DeleteTimerKodi|%u|%d", xTmr.iClientIndex, bRepeating);
	
	vector<std::string> results = _socketClient.GetVector(command, false);	// get results from server

	PVR->TriggerTimerUpdate();									// update timers regardless of whether there is an error

	if (isServerError(results))									// did the server do it?
	{
		return PVR_ERROR_SERVER_ERROR;
	}
	else
	{
		XBMC->Log(LOG_DEBUG, "deleted timer '%s', with rec state %s", xTmr.strTitle, results[0].c_str());
		return PVR_ERROR_NO_ERROR;
	}
}

PVR_ERROR Pvr2Wmc::GetTimers(ADDON_HANDLE handle)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	vector<std::string> responsesSeries = _socketClient.GetVector("GetSeriesTimers", true);
	FOREACH(response, responsesSeries)
	{
		PVR_TIMER xTmr;
		memset(&xTmr, 0, sizeof(PVR_TIMER));						// set all struct to zero

		vector<std::string> v = split(*response, "|");				// split to unpack string
		if (v.size() < 24)
		{
			XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for SeriesTimer data");
			continue;
		}

		xTmr.iTimerType = PVR_TIMER_TYPE_NONE;
																// [0] Timer ID (need UINT32 value, see [21])
																// [1] Title (superceded by [17] Timer Name)
		xTmr.iClientChannelUid = atoi(v[2].c_str());			// [2] channel id
		xTmr.iEpgUid = atoi(v[3].c_str());						// [3] epg ID (same as client ID, except for a 'manual' record)	
		STRCPY(xTmr.strSummary, v[4].c_str());					// [4] currently set to description
		xTmr.startTime = atoi(v[5].c_str());					// [5] start time 
		xTmr.endTime = atoi(v[6].c_str());						// [6] end time 
		xTmr.iMarginStart = atoi(v[7].c_str());					// [7] rec margin at start (sec)
		xTmr.iMarginEnd = atoi(v[8].c_str());					// [8] rec margin at end (sec)
																// [9] isPreMarginRequired
																// [10] isPostMarginRequired
																// [11] WMC Priority (need Kodi compatible value, see [26])
																// [12] NewEpisodesOnly (superceded by RunType)
		if (Str2Bool(v[13].c_str()))							// [13] Any Channel
		{
			xTmr.iClientChannelUid = 0;
		}
		if (Str2Bool(v[14].c_str()))							// [14] Any Time
		{
			xTmr.bStartAnyTime = true;
			xTmr.bEndAnyTime = true;
		}
		xTmr.iWeekdays = atoi(v[15].c_str());					// [15] DaysOfWeek (converted to Kodi values in the backend)
		xTmr.state = (PVR_TIMER_STATE)atoi(v[16].c_str());		// [16] current state of timer
		STRCPY(xTmr.strTitle, v[17].c_str());					// [17] timer name
		xTmr.iGenreType = atoi(v[18].c_str());					// [18] genre ID
		xTmr.iGenreSubType = atoi(v[19].c_str());				// [19] sub genre ID
		xTmr.iPreventDuplicateEpisodes = atoi(v[20].c_str());	// [20] WMC RunType
		xTmr.iClientIndex = atoi(v[21].c_str());				// [21] Timer ID (in UINT32 form)
		STRCPY(xTmr.strEpgSearchString, v[22].c_str());			// [22] Keyword Search
		xTmr.bFullTextEpgSearch = Str2Bool(v[23].c_str());		// [23] Keyword is FullText
		xTmr.iLifetime = atoi(v[24].c_str());					// [24] Lifetime
		xTmr.iMaxRecordings = atoi(v[25].c_str());				// [25] Maximum Recordings (Recording Limit)
		xTmr.iPriority = atoi(v[26].c_str());					// [26] Priority (in Kodi enum value form)

		// Determine TimerType
		bool hasKeyword = strlen(xTmr.strEpgSearchString) > 0;
		bool hasEPG = (xTmr.iEpgUid != PVR_TIMER_NO_EPG_UID);
		xTmr.iTimerType = hasKeyword ? TIMER_REPEATING_KEYWORD : hasEPG ? TIMER_REPEATING_EPG : TIMER_REPEATING_MANUAL;

		PVR->TransferTimerEntry(handle, &xTmr);
	}

	vector<std::string> responsesTimers = _socketClient.GetVector("GetTimers", true);
	FOREACH(response, responsesTimers)
	{
		PVR_TIMER xTmr;
		memset(&xTmr, 0, sizeof(PVR_TIMER));						// set all struct to zero

		vector<std::string> v = split(*response, "|");				// split to unpack string
		// eId, chId, start_t, end_t, pState,
		// rp.Program.Title, ""/*recdir*/, rp.Program.EpisodeTitle/*summary?*/, rp.Priority, rp.Request.IsRecurring,
		// eId, preMargin, postMargin, genre, subgenre

		if (v.size() < 24)
		{
			XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for timer data");
			continue;
		}

		xTmr.iTimerType = PVR_TIMER_TYPE_NONE;
		xTmr.iClientIndex = atoi(v[0].c_str());					// [0] Timer ID
		xTmr.iClientChannelUid = atoi(v[1].c_str());			// [1] channel id
		xTmr.startTime = atoi(v[2].c_str());					// [2] start time 
		xTmr.endTime = atoi(v[3].c_str());						// [3] end time 
		xTmr.state = (PVR_TIMER_STATE)atoi(v[4].c_str());		// [4] current state of time
		STRCPY(xTmr.strTitle, v[5].c_str());					// [5] timer name (set to same as Program title)
		STRCPY(xTmr.strDirectory, v[6].c_str());				// [6] rec directory
		STRCPY(xTmr.strSummary, v[7].c_str());					// [7] set to program description
																// [8] WMC Priority (need Kodi compatible value, see [26])
																// [9] IsRecurring
		xTmr.iEpgUid = atoi(v[10].c_str());						// [10] epg ID
		xTmr.iMarginStart = atoi(v[11].c_str());				// [11] rec margin at start (sec)
		xTmr.iMarginEnd = atoi(v[12].c_str());					// [12] rec margin at end (sec)
		xTmr.iGenreType = atoi(v[13].c_str());					// [13] genre ID
		xTmr.iGenreSubType = atoi(v[14].c_str());				// [14] sub genre ID
																// [15] epg ID (duplicated from [9] for some reason)
																// [16] Parent Series ID (need in UINT32 form, see [23])
																// [17] isPreMarginRequired
																// [18] isPostMarginRequired
		xTmr.iPreventDuplicateEpisodes = atoi(v[19].c_str());	// [19] WMC runType
		if (Str2Bool(v[20].c_str()))							// [20] Any Channel
		{
			// As this is a child instance recording, we want to preserve the actual channel
		}
		if (Str2Bool(v[21].c_str()))							// [21] Any Time
		{
			// As this is a child instance recording, we want to preserve the actual start/finish times
		}
		xTmr.iWeekdays = atoi(v[22].c_str());					// [22] DaysOfWeek (converted to Kodi values in the backend)
		xTmr.iParentClientIndex = atoi(v[23].c_str());			// [23] Parent Series ID (in UINT32 form)
		xTmr.iLifetime = atoi(v[24].c_str());					// [24] Lifetime
		xTmr.iMaxRecordings = atoi(v[25].c_str());				// [25] Maximum Recordings (Recording Limit)
		xTmr.iPriority = atoi(v[26].c_str());					// [26] Priority (in Kodi enum value form)
		STRCPY(xTmr.strEpgSearchString, v[27].c_str());			// [27] Keyword Search
		xTmr.bFullTextEpgSearch = Str2Bool(v[28].c_str());		// [28] Keyword is FullText

		// Determine TimerType
		bool hasParent = (xTmr.iParentClientIndex != 0);
		bool hasKeyword = strlen(xTmr.strEpgSearchString) > 0;
		bool hasEPG = (xTmr.iEpgUid != PVR_TIMER_NO_EPG_UID);
		if (hasParent)
		{
			xTmr.iTimerType = hasKeyword ? TIMER_ONCE_KEYWORD_CHILD : hasEPG ? TIMER_ONCE_EPG_CHILD : TIMER_ONCE_MANUAL_CHILD;
		}
		else
		{
			xTmr.iTimerType = hasKeyword ? TIMER_ONCE_KEYWORD : hasEPG ? TIMER_ONCE_EPG : TIMER_ONCE_MANUAL;
		}

		PVR->TransferTimerEntry(handle, &xTmr);
	}

	// check time since last time Recordings were updated, update if it has been awhile
	if ( _lastRecordingUpdateTime != 0 && P8PLATFORM::GetTimeMs() > _lastRecordingUpdateTime + 120000)
	{
		PVR->TriggerRecordingUpdate();
	}
	return PVR_ERROR_NO_ERROR;
}

// recording functions ------------------------------------------------------------------------
int Pvr2Wmc::GetRecordingsAmount(void)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	return _socketClient.GetInt("GetRecordingsAmount", true);
}

// recording file  functions
PVR_ERROR Pvr2Wmc::GetRecordings(ADDON_HANDLE handle)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	vector<std::string> responses = _socketClient.GetVector("GetRecordings", true);				

	FOREACH(response, responses)
	{
		PVR_RECORDING xRec;
		memset(&xRec, 0, sizeof(PVR_RECORDING));					// set all struct to zero

		vector<std::string> v = split(*response, "|");				// split to unpack string

		// r.Id, r.Program.Title, r.FileName, recDir, plotOutline,
		// plot, r.Channel.CallSign, ""/*icon path*/, ""/*thumbnail path*/, ToTime_t(r.RecordingTime),
		// duration, r.RequestedProgram.Priority, r.KeepLength.ToString(), genre, subgenre, ResumePos
		// fields 16 - 23 used by MB3, 24 PlayCount

		if (v.size() < 16)
		{
			XBMC->Log(LOG_DEBUG, "Wrong number of fields xfered for recording data");
			continue;
		}

		STRCPY(xRec.strRecordingId, v[0].c_str());
		STRCPY(xRec.strTitle, v[1].c_str());
		STRCPY(xRec.strDirectory, v[3].c_str());
		STRCPY(xRec.strPlotOutline, v[4].c_str());
		STRCPY(xRec.strPlot, v[5].c_str());
		STRCPY(xRec.strChannelName, v[6].c_str());
		STRCPY(xRec.strIconPath, v[7].c_str());
		STRCPY(xRec.strThumbnailPath, v[8].c_str());
		xRec.recordingTime = atol(v[9].c_str());
		xRec.iDuration = atoi(v[10].c_str());
		xRec.iPriority = atoi(v[11].c_str());
		xRec.iLifetime = atoi(v[12].c_str());
		xRec.iGenreType = atoi(v[13].c_str());
		xRec.iGenreSubType = atoi(v[14].c_str());
		if (g_bEnableMultiResume)
		{
			xRec.iLastPlayedPosition = atoi(v[15].c_str());
			if (v.size() > 24)
			{
				xRec.iPlayCount = atoi(v[24].c_str());
			}
		}

		// Kodi PVR API 1.9.5 adds EPG ID field
		if (v.size() > 19)
		{
			xRec.iEpgEventId = atoi(v[18].c_str());
		}

		// Kodi PVR API 5.0.0 adds EPG ID field
		if (v.size() > 18)
		{
			xRec.iChannelUid = atoi(v[17].c_str());
		}
		else
		{
			xRec.iChannelUid = PVR_CHANNEL_INVALID_UID;
		}

		/* TODO: PVR API 5.1.0: Implement this */
		xRec.channelType = PVR_RECORDING_CHANNEL_TYPE_UNKNOWN;

		PVR->TransferRecordingEntry(handle, &xRec);
	}

	_lastRecordingUpdateTime = P8PLATFORM::GetTimeMs();

	return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Pvr2Wmc::DeleteRecording(const PVR_RECORDING &recording)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string command;
	command = string_format("DeleteRecording|%s|%s|%s", recording.strRecordingId, recording.strTitle, "");

	vector<std::string> results = _socketClient.GetVector(command, false);	// get results from server


	if (isServerError(results))							// did the server do it?
	{
		return PVR_ERROR_NO_ERROR;						// report "no error" so our error shows up
	}
	else
	{
		TriggerUpdates(results);
		//PVR->TriggerRecordingUpdate();					// tell xbmc to update recording display
		XBMC->Log(LOG_DEBUG, "deleted recording '%s'", recording.strTitle);

		//if (results.size() == 2 && results[0] == "updateTimers")	// if deleted recording was actually recording a the time
		//	PVR->TriggerTimerUpdate();								// update timer display too

		return PVR_ERROR_NO_ERROR;
	}
}


PVR_ERROR Pvr2Wmc::RenameRecording(const PVR_RECORDING &recording)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string command;// = format("RenameRecording|%s|%s", recording.strRecordingId, recording.strTitle);
	command = string_format("RenameRecording|%s|%s", recording.strRecordingId, recording.strTitle);

	vector<std::string> results = _socketClient.GetVector(command, false);					// get results from server

	if (isServerError(results))							// did the server do it?
	{
		return PVR_ERROR_NO_ERROR;						// report "no error" so our error shows up
	}
	else
	{
		TriggerUpdates(results);
		XBMC->Log(LOG_DEBUG, "deleted recording '%s'", recording.strTitle);
		return PVR_ERROR_NO_ERROR;
	}
}

// set the recording resume position in the wmc database
PVR_ERROR Pvr2Wmc::SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string command;
	command = string_format("SetResumePosition|%s|%d", recording.strRecordingId, lastplayedposition);
	
	vector<std::string> results = _socketClient.GetVector(command, true);					
	PVR->TriggerRecordingUpdate();		// this is needed to get the new resume point actually used by the player (xbmc bug)								
	return PVR_ERROR_NO_ERROR;
}

// get the rercording resume position from the wmc database
// note: although this resume point time will be displayed to the user in the gui (in the resume dlog)
// the return value is ignored by the xbmc player.  That's why TriggerRecordingUpdate is required in the setting above
int Pvr2Wmc::GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string command;
	command = string_format("GetResumePosition|%s", recording.strRecordingId);
	int pos = _socketClient.GetInt(command, true);
	return pos;
}

// set the recording playcount in the wmc database
PVR_ERROR Pvr2Wmc::SetRecordingPlayCount(const PVR_RECORDING &recording, int count)
{
	if (IsServerDown())
		return PVR_ERROR_SERVER_ERROR;

	std::string command;
	command = string_format("SetPlayCount|%s|%d", recording.strRecordingId, count);
	vector<std::string> results = _socketClient.GetVector(command, true);					
	if (count <= 0)
		PVR->TriggerRecordingUpdate();		// this is needed to get the new play count actually used by the player (xbmc bug)								
	return PVR_ERROR_NO_ERROR;
}


std::string Pvr2Wmc::Channel2String(const PVR_CHANNEL &xCh)
{
	// packing: id, bradio, c.OriginalNumber, c.CallSign, c.IsEncrypted, imageStr, c.IsBlocked
	std::string chStr;
	chStr = string_format("|%u|%d|%u|%s", xCh.iUniqueId, xCh.bIsRadio, xCh.iChannelNumber, xCh.strChannelName);
	return chStr;
}

// live/recorded stream functions --------------------------------------------------------------

bool Pvr2Wmc::OpenLiveStream(const PVR_CHANNEL &channel)
{
	if (IsServerDown())
		return false;

	_lostStream = true;								// init
	_readCnt = 0;
	int _buffTimesCnt = 0;							
	int _buffTimeFILTER = 0;

	CloseLiveStream(false);							// close current stream (if any)

	std::string request = "OpenLiveStream" + Channel2String(channel);		// request a live stream using channel
	vector<std::string> results = _socketClient.GetVector(request, false);	// try to open live stream, get path to stream file

	if (isServerError(results))												// test if server reported an error
	{
		return false;
	} 
	else 
	{
		_streamFileName = results[0];								// get path of streamed file
		_streamWTV = EndsWith(results[0], "wtv");					// true if stream file is a wtv file

		if (results.size() > 1)
			XBMC->Log(LOG_DEBUG, "OpenLiveStream> opening stream: %s", results[1].c_str());		// log password safe path of client if available
		else
			XBMC->Log(LOG_DEBUG, "OpenLiveStream> opening stream: %s", _streamFileName.c_str());
		
		// Initialise variables for starting stream at an offset
		_initialStreamResetCnt = 0;
		_initialStreamPosition = 0;

		// Check for a specified initial position and save it for the first ReadLiveStream command to use
		if (results.size() > 2)
		{
			_initialStreamPosition = atoll(results[2].c_str());
		}

		_streamFile = XBMC->OpenFile(_streamFileName.c_str(), 0);	// open the video file for streaming, same handle

		if (!_streamFile)	// something went wrong
		{
			std::string lastError;
#ifdef TARGET_WINDOWS
			int errorVal = GetLastError();
			lastError = string_format("Error opening stream file, Win32 error code: %d", errorVal);
#else
			lastError = "Error opening stream file";
#endif
			XBMC->Log(LOG_ERROR, lastError.c_str());						// log more info on error
			
			_socketClient.GetBool("StreamStartError|" + _streamFileName, true);	// tell server stream did not start

			return false;
		}
		else
		{
			_discardSignalStatus = false;			// reset signal status discard flag
			XBMC->Log(LOG_DEBUG, "OpenLiveStream> stream file opened successfully");
		}

		_lostStream = false;						// if we got to here, stream started ok, so set default values
		_lastStreamSize = 0;
		_isStreamFileGrowing = true;
		_insertDurationHeader = false;				// only used for active recordings
		return true;								// stream is up
	}
}


// read from the live stream file opened in OpenLiveStream
int Pvr2Wmc::ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
	if (_lostStream)									// if stream has already been flagged as lost, return 0 bytes 
		return 0;										// after this happens a few time, xbmc will call CloseLiveStream

	_readCnt++;											// keep a count of the number of reads executed

	if (!_streamWTV)									// if NOT streaming wtv, make sure stream is big enough before it is read
	{						
		int timeout = 0;								// reset timeout counter

		// If we are trying to skip to an initial start position (eg we are watching an existing live stream
		// in a multiple client scenario), we need to do it here, as the Seek command didnt work in OpenLiveStream,
		// XBMC just started playing from the start of the file anyway.  But once the stream is open, XBMC repeatedly 
		// calls ReadLiveStream and a Seek() command done here DOES get actioned.
		// 
		// So the first time we come in here, we can Seek() to our desired start offset.
		//
		// However I found the XBMC demuxer makes an initial pass first and then sets the position back to 0 again and makes a 2nd pass,
		// So we actually need to Seek() to our initial start position more than once.  Because there are other situations where we can end up 
		// at the start of the file (such as the user rewinding) the easiest option at this point is to simply assume the demuxer makes 2 passes,
		// and to reset the Seek position twice before clearing the stored value and thus no longer performing the reset.

		// Seek to initial file position if OpenLiveStream stored a starting offset and we are at position 0 (start of the file)
		if (_initialStreamPosition > 0 && PositionLiveStream() == 0)
		{
			long long newPosition = XBMC->SeekFile(_streamFile, _initialStreamPosition, SEEK_SET);
			if (newPosition == _initialStreamPosition)
			{
				XBMC->Log(LOG_DEBUG, "ReadLiveStream> stream file seek to initial position %llu successful", _initialStreamPosition);
			}
			else
			{
				XBMC->Log(LOG_DEBUG, "ReadLiveStream> stream file seek to initial position %llu failed (new position: %llu)", _initialStreamPosition, newPosition);
			}

			_initialStreamResetCnt++;
			if (_initialStreamResetCnt >= 2)
			{
				_initialStreamPosition = 0;				// reset stored value to 0 once we have performed 2 resets (2 pass demuxer)
			}
		}

		long long currentPos = PositionLiveStream();	// get the current file position

		// this is a hack to set the time duration of an ACTIVE recording file. Xbmc reads the ts duration by looking for timestamps
		// (pts/dts) toward the end of the ts file.  Since our ts file is very small at the start, xbmc skips trying to get the duration
		// and just sets it to zero, this makes FF,RW,Skip work poorly and gives bad OSD feedback during playback.  The hack is 
		// to tell xbmc that the ts file is 'FAKE_TS_LENGTH' in length at the start of the playback (see LengthLiveStream).  Then when xbmc 
		// tries to read the duration by probing the end of the file (it starts looking at fileLength-250k), we catch this read below and feed
		// it a packet that contains a pts with the duration we want (this packet header is received from the server).  After this, everything 
		// is set back to normal.
		if (_insertDurationHeader && FAKE_TS_LENGTH - 250000 == currentPos) // catch xbmc probing for timestamps at end of file
		{
			//char pcr[16] = {0x47, 0x51, 0x00, 0x19, 0x00, 0x00, 0x01, 0xBD, 0x00, 0x00, 0x85, 0x80, 0x05, 0x21, 0x2E, 0xDF};
			_insertDurationHeader = false;									// only do header insertion once
			memset(pBuffer, 0xFF, iBufferSize);								// set buffer to all FF (default padding char for packets)
			vector<std::string> v = split(_durationHeader, " ");				// get header bytes by unpacking reponse
			for (int i=0; i<16; i++)										// insert header bytes, creating a fake packet at start of buffer
			{
				//*(pBuffer + i) = pcr[i];
				*(pBuffer + i) = (char)strtol(v[i].c_str(), NULL, 16);
			}
			return iBufferSize;												// terminate read here after header is inserted
		} 
		// in case something goes wrong, turn off fake header insertion flag.
		// the header insertion usually happens around _readCnt=21, so 50 should be safe
		if (_readCnt > 50)
			_insertDurationHeader = false;

		long long fileSize = _lastStreamSize;			// use the last fileSize found, rather than querying host

		if (_isStreamFileGrowing && currentPos + iBufferSize > fileSize)	// if its not big enough for the readsize
			fileSize = ActualFileSize(timeout);								// get the current size of the stream file

		// if the stream file is growing, see if the stream file is big enough to accomodate this read
		// if its not, wait until it is
		while (_isStreamFileGrowing && currentPos + iBufferSize > fileSize)
		{
			usleep(600000);								// wait a little (600ms) before we try again
			timeout++;
			fileSize = ActualFileSize(timeout);			// get new file size after delay

			if (!_isStreamFileGrowing)					// if streamfile is no longer growing...
			{
				if (CheckErrorOnServer())				// see if server says there is an error
				{
					_lostStream = true;					// if an error was posted, close the stream down
					return -1;																
				}
				else
					break;								// terminate loop since stream file isn't growing no sense in waiting
			}
			else if (fileSize == -1)					// if fileSize -1, server is reporting an 'unkown' error with the stream
			{
				XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30003));	// display generic error with stream
				XBMC->Log(LOG_DEBUG, "live tv error, server reported error");
				_lostStream = true;														// flag that stream is down
				return -1;																
			}

			if (timeout > 50 )									// if after 30 sec the file has not grown big enough, timeout
			{
				_lostStream = true;								// flag that stream is down
				if (currentPos == 0 && fileSize == 0)			// if no data was ever read, assume no video signal
				{
					XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30004));
					XBMC->Log(LOG_DEBUG, "no video found for stream");
				} 
				else											// a mysterious reason caused timeout
				{
					XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30003));	// otherwise display generic error
					XBMC->Log(LOG_DEBUG, "live tv timed out, unknown reason");
				}
				return -1;																	// this makes xbmc call closelivestream
			}
		}
	}  // !_streamWTV

	// finally, read data from stream file
	unsigned int lpNumberOfBytesRead = XBMC->ReadFile(_streamFile, pBuffer, iBufferSize);

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
		vector<std::string> results = _socketClient.GetVector(request, true);	// see if server posted an error for active stream
		return isServerError(results);
	}
	return false;
}

//#define SEEK_SET	0
//#define SEEK_CUR	1
//#define SEEK_END	2
long long Pvr2Wmc::SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) 
{
	int64_t lFilePos = 0;
	if (_streamFile != 0)
	{
		lFilePos = XBMC->SeekFile(_streamFile, iPosition, iWhence);
	}
	return lFilePos;
}

// return the current file pointer position
long long Pvr2Wmc::PositionLiveStream(void) 
{
	int64_t lFilePos = -1;
	if (_streamFile != 0)
	{
		lFilePos = XBMC->GetFilePosition(_streamFile);
	}
	return lFilePos;
}

// get stream file size, querying it from server if needed
long long Pvr2Wmc::ActualFileSize(int count)
{
	long long lFileSize = 0;

	if (_lostStream)									// if stream was lost, return 0 file size (may not be needed)
		return 0;

	if (!_isStreamFileGrowing)							// if stream file is no longer growing, return the last stream size
	{
		lFileSize = _lastStreamSize;
	}
	else
	{
		std::string request;
		request = string_format("StreamFileSize|%d", count);		// request stream size form client, passing number of consecutive queries
		lFileSize = _socketClient.GetLL(request, true);	// get file size form client

		if (lFileSize < -1)								// if server returns a negative file size, it means the stream file is no longer growing (-1 => error)
		{
			lFileSize = -lFileSize;						// make stream file length positive
			_isStreamFileGrowing = false;				// flag that stream file is no longer growing
		}
		_lastStreamSize = lFileSize;					// save this stream size
	}
	return lFileSize;
}

// return the length of the current stream file
long long Pvr2Wmc::LengthLiveStream(void)
{
	if (_insertDurationHeader)			// if true, return a fake file 2Mb length to xbmc, this makes xbmc try to determine
		return FAKE_TS_LENGTH;			// the ts time duration giving us a chance to insert the real duration
	if (_lastStreamSize > 0)
		return _lastStreamSize;
	return -1;
}

void Pvr2Wmc::PauseStream(bool bPaused)
{
}

bool Pvr2Wmc::CloseLiveStream(bool notifyServer /*=true*/)
{
	if (IsServerDown())
		return false;

	if (_streamFile != 0)						// if file is still open, close it
		XBMC->CloseFile(_streamFile);

	_streamFile = 0;							// file handle now closed
	_streamFileName = "";

	_lostStream = true;							// for cleanliness

	if (notifyServer)
	{	
		return _socketClient.GetBool("CloseLiveStream", false);		// tell server to close down stream
	}
	else
		return true;
}


bool Pvr2Wmc::OpenRecordedStream(const PVR_RECORDING &recording)
{
	if (IsServerDown())
		return false;

	_lostStream = true;								// init
	_readCnt = 0;
	int _buffTimesCnt = 0;
	int _buffTimeFILTER = 0;

	// request an active recording stream
	std::string request;
	request = string_format("OpenRecordingStream|%s", recording.strRecordingId);
	vector<std::string> results = _socketClient.GetVector(request, false);	// try to open recording stream, get path to stream file

	if (isServerError(results))								// test if server reported an error
	{
		return false;
	} 
	else 
	{
		_streamFileName = results[0];
		_streamWTV = EndsWith(_streamFileName, "wtv");		// true if stream file is a wtv file

		// hand additional args from server
		if (results.size() >  1)
			XBMC->Log(LOG_DEBUG, "OpenRecordedStream> rec stream type: %s", results[1].c_str());		// either a 'passive' or 'active' WTV OR a TS file
		
		if (results.size() > 2)
			XBMC->Log(LOG_DEBUG, "OpenRecordedStream> opening stream: %s", results[2].c_str());		// log password safe path of client if available
		else
			XBMC->Log(LOG_DEBUG, "OpenRecordedStream> opening stream: %s", _streamFileName.c_str());	

		if (results.size() > 3 && results[3] != "")											// get header to set duration of ts file
		{
			_durationHeader = results[3];													
			_insertDurationHeader = true;
		}
		else
		{
			_durationHeader = "";
			_insertDurationHeader = false;
		}

		_streamFile = XBMC->OpenFile(_streamFileName.c_str(), 0);	// open the video file for streaming, same handle

		if (!_streamFile)	// something went wrong
		{
			std::string lastError;
#ifdef TARGET_WINDOWS
			int errorVal = GetLastError();
			lastError = string_format("Error opening stream file, Win32 error code: %d", errorVal);
#else
			lastError = "Error opening stream file";
#endif
			XBMC->Log(LOG_ERROR, lastError.c_str());						// log more info on error
			_socketClient.GetBool("StreamStartError|" + _streamFileName, true);	// tell server stream did not start
			return false;
		}
		else
			XBMC->Log(LOG_DEBUG, "OpenRecordedStream> stream file opened successfully");

		_lostStream = false;						// stream is open
		_lastStreamSize = 0;						// current size is empty
		_isStreamFileGrowing = true;				// initially assume its growing
		ActualFileSize(0);							// get initial file size from swmc, also tells it file was opened successfully

		// Initialise variables for starting stream at an offset (only used for live streams)
		_initialStreamResetCnt = 0;
		_initialStreamPosition = 0;

		return true;								// if we got to here, stream started ok
	}
}

PVR_ERROR Pvr2Wmc::SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
	if (!g_bSignalEnable || _discardSignalStatus)
	{
		return PVR_ERROR_NO_ERROR;
	}

	static PVR_SIGNAL_STATUS cachedSignalStatus;

	// Only send request to backend every N times
	if (_signalStatusCount-- <= 0)
	{
		if (IsServerDown())
			return PVR_ERROR_SERVER_ERROR;

		// Reset count to throttle value
		_signalStatusCount = g_signalThrottle;

		std::string command;
		command = "SignalStatus";

		vector<std::string> results = _socketClient.GetVector(command, true);	// get results from server

		// strDeviceName, strDeviceStatus, strProvider, strService, strMux
		// iSignal, dVideoBitrate, dAudioBitrate, Error

		if (isServerError(results))							// did the server do it?
		{
			return PVR_ERROR_SERVER_ERROR;					// report "no error" so our error shows up
		}
		else
		{
			if (results.size() >= 9)
			{
				memset(&cachedSignalStatus, 0, sizeof(cachedSignalStatus));
				snprintf(cachedSignalStatus.strAdapterName, sizeof(cachedSignalStatus.strAdapterName), "%s", results[0].c_str());
				snprintf(cachedSignalStatus.strAdapterStatus, sizeof(cachedSignalStatus.strAdapterStatus), "%s", results[1].c_str());
				snprintf(cachedSignalStatus.strProviderName, sizeof(cachedSignalStatus.strProviderName), "%s", results[2].c_str());
				snprintf(cachedSignalStatus.strServiceName, sizeof(cachedSignalStatus.strServiceName), "%s", results[3].c_str());
				snprintf(cachedSignalStatus.strMuxName, sizeof(cachedSignalStatus.strMuxName), "%s", results[4].c_str());
				cachedSignalStatus.iSignal = (int)(atoi(results[5].c_str()) * 655.35);
			
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


bool Pvr2Wmc::IsTimeShifting()
{
	if (_streamFile)		// ?not sure if this should be false if playtime is at buffer end)
		return true;		
	else
		return false;
}

time_t _buffStart;
time_t _buffEnd;
time_t _buffCurrent;

// get current playing time from swmc remux, this method also recieves buffer start and buffer end
// to minimize traffic to swmc, since Kodi calls this like crazy, the calls are filtered.  The size
// of the filter is passed in from swmc
time_t Pvr2Wmc::GetPlayingTime()
{
	if (_streamFile && _buffTimesCnt >= _buffTimeFILTER)				// filter so we don't query swmc too much
	{
		_buffTimesCnt = 0;
		int64_t filePos = XBMC->GetFilePosition(_streamFile);			// get the current file pos so we can convert to play time
		std::string request;
		request = string_format("GetBufferTimes|%llu", filePos);
		vector<std::string> results = _socketClient.GetVector(request, false);	// have swmc convert file pos to current play time

		if (results.size() > 3)
		{
			_buffStart = atol(results[0].c_str());
			_buffEnd = atol(results[1].c_str());
			_buffCurrent = atol(results[2].c_str());
			_buffTimeFILTER = atoi(results[3].c_str());		// get filter value from swmc
		}
	}
	_buffTimesCnt++;
	return _buffCurrent;
}

time_t Pvr2Wmc::GetBufferTimeStart()
{
	if (_streamFile)
	{
		return _buffStart;
	}
	return 0;
}

time_t Pvr2Wmc::GetBufferTimeEnd()
{
	if (_streamFile)
	{
		return _buffEnd;
	}
	return 0;
}
