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

#include "client.h"
#include "xbmc_pvr_dll.h"
#include "pvr2wmc.h"
#include "utilities.h"
#include "p8-platform/util/util.h"

using namespace ADDON;


#define DEFAULT_PORT 9080
#define DEFAULT_WAKEONLAN_ENABLE false
#define DEFAULT_SIGNAL_ENABLE false
#define DEFAULT_SIGNAL_THROTTLE 10
#define DEFAULT_MULTI_RESUME true

Pvr2Wmc*		_wmc			= NULL;
bool			_bCreated       = false;
ADDON_STATUS	_CurStatus      = ADDON_STATUS_UNKNOWN;
bool			_bIsPlaying     = false;
PVR_CHANNEL		_currentChannel;
PVR_MENUHOOK	*menuHook       = NULL;

std::string		g_strServerName;							// the name of the server to connect to
std::string		g_strClientName;							// the name of the computer running addon
int				g_port;
bool			g_bWakeOnLAN;								// whether to send wake on LAN to server
std::string		g_strServerMAC;								// MAC address of server
bool			g_bSignalEnable;
int				g_signalThrottle;
bool			g_bEnableMultiResume;
std::string		g_clientOS;									// OS of client, passed to server

backend_status	g_BackendOnline;							// whether the backend is online

/* User adjustable settings are saved here.
* Default values are defined inside client.h
* and exported to the other source files.
*/
std::string g_strUserPath             = "";
std::string g_strClientPath           = "";
std::string	g_AddonDataCustom	= "";						// location of custom addondata settings file

CHelper_libXBMC_addon   *XBMC           = NULL;
CHelper_libXBMC_pvr   	*PVR            = NULL;

#define LOCALHOST "127.0.0.1"

extern "C" {

	void ADDON_ReadSettings(void)
	{
		char buffer[512];

		if (!XBMC)
			return;

		g_strServerName = LOCALHOST;
		g_strServerMAC = "";
		g_bWakeOnLAN = false;
		g_port = DEFAULT_PORT;
		g_bSignalEnable = DEFAULT_SIGNAL_ENABLE;
		g_signalThrottle = DEFAULT_SIGNAL_THROTTLE;
		g_bEnableMultiResume = DEFAULT_MULTI_RESUME;

		/* Read setting "port" from settings.xml */
		if (!XBMC->GetSetting("port", &g_port))
		{
			XBMC->Log(LOG_ERROR, "Couldn't get 'port' setting, using '%i'", DEFAULT_PORT);
		}

		if (XBMC->GetSetting("host", &buffer))
		{ 
			g_strServerName = buffer;
			XBMC->Log(LOG_DEBUG, "Settings: host='%s', port=%i", g_strServerName.c_str(), g_port);
		}
		else
		{
			XBMC->Log(LOG_ERROR, "Couldn't get 'host' setting, using '127.0.0.1'");
		}

		if (!XBMC->GetSetting("wake_on_lan", &g_bWakeOnLAN))
		{
			XBMC->Log(LOG_ERROR, "Couldn't get 'wake_on_lan' setting, using '%s'", DEFAULT_WAKEONLAN_ENABLE);
		}

		std::string fileContent;
		if (ReadFileContents(g_AddonDataCustom, fileContent))
		{
			g_strServerMAC = fileContent;
			XBMC->Log(LOG_ERROR, "Using ServerWMC MAC address from custom addondata '%s'", g_strServerMAC.c_str());
		}
		else
		{
			XBMC->Log(LOG_ERROR, "Couldn't get ServerWMC MAC address from custom addondata, using empty value");
		}

		if (!XBMC->GetSetting("signal", &g_bSignalEnable))
		{
			XBMC->Log(LOG_ERROR, "Couldn't get 'signal' setting, using '%s'", DEFAULT_SIGNAL_ENABLE);
		}

		if (!XBMC->GetSetting("signal_throttle", &g_signalThrottle))
		{
			XBMC->Log(LOG_ERROR, "Couldn't get 'signal_throttle' setting, using '%s'", DEFAULT_SIGNAL_THROTTLE);
		}
		
		if (!XBMC->GetSetting("multiResume", &g_bEnableMultiResume))
		{
			XBMC->Log(LOG_ERROR, "Couldn't get 'multiResume' setting, using '%s'", DEFAULT_MULTI_RESUME);
		}
		

		// get the name of the computer client is running on
		gethostname(buffer, 50); 
		g_strClientName = buffer;		// send this computers name to server

#ifdef TARGET_WINDOWS
		g_clientOS = "windows";			// set to the client OS name
#elif defined TARGET_LINUX
		g_clientOS = "linux";			// set to the client OS name
#elif defined TARGET_DARWIN
		g_clientOS = "darwin";			// set to the client OS name
#elif defined TARGET_FREEBSD
		g_clientOS = "freeBSD";			// set to the client OS name
#else
		g_clientOS = "";				// set blank client OS name
#endif
	}

	// create this addon (called by xbmc)
	ADDON_STATUS ADDON_Create(void* hdl, void* props)
	{
		if (!hdl || !props)
			return ADDON_STATUS_UNKNOWN;

		PVR_PROPERTIES* pvrprops = (PVR_PROPERTIES*)props;

		// register the addon
		XBMC = new CHelper_libXBMC_addon;

		if (!XBMC->RegisterMe(hdl))
		{
			SAFE_DELETE(XBMC);
			return ADDON_STATUS_PERMANENT_FAILURE;
		}

		// register as pvr
		PVR = new CHelper_libXBMC_pvr;	
		if (!PVR->RegisterMe(hdl))
		{
			SAFE_DELETE(PVR);
			SAFE_DELETE(XBMC);
			return ADDON_STATUS_PERMANENT_FAILURE;
		}

		XBMC->Log(LOG_DEBUG, "%s - Creating the PVR-WMC add-on", __FUNCTION__);

		_CurStatus     = ADDON_STATUS_UNKNOWN;
		g_strUserPath   = pvrprops->strUserPath;
		g_strClientPath = pvrprops->strClientPath;
		g_AddonDataCustom = g_strUserPath + "ServerMACAddr.txt";

		ADDON_ReadSettings();

		_wmc = new Pvr2Wmc;								// create interface to ServerWMC
		if (_wmc->IsServerDown())						// check if server is down, if it is shut her down
		{
			SAFE_DELETE(_wmc);
			SAFE_DELETE(PVR);
			SAFE_DELETE(XBMC);
			_CurStatus = ADDON_STATUS_LOST_CONNECTION;
		}
		else
		{
			_bCreated = true;
			_CurStatus = ADDON_STATUS_OK;

		}
		return _CurStatus;
	}

	// get status of addon's interface to wmc server
	ADDON_STATUS ADDON_GetStatus()
	{
		// check whether we're still connected 
		if (_CurStatus == ADDON_STATUS_OK)
		{
			if (_wmc == NULL)
				_CurStatus = ADDON_STATUS_LOST_CONNECTION;
			else if (_wmc->IsServerDown())
				_CurStatus = ADDON_STATUS_LOST_CONNECTION;
		}

		return _CurStatus;
	}

	void ADDON_Destroy()
	{
		if (_wmc)
			_wmc->UnLoading();
		SAFE_DELETE(PVR);
		_bCreated = false;
		_CurStatus = ADDON_STATUS_UNKNOWN;
	}

	// Called everytime a setting is changed by the user and to inform AddOn about
	// new setting and to do required stuff to apply it.
	ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
	{
		if (!XBMC)
			return ADDON_STATUS_OK;

		std::string sName = settingName;

		if (sName == "host")
		{
			std::string oldName = g_strServerName;
			g_strServerName = (const char*)settingValue;

			XBMC->Log(LOG_INFO, "Setting 'host' changed from %s to %s", g_strServerName.c_str(), (const char*) settingValue);
			if (oldName != g_strServerName)
				return ADDON_STATUS_NEED_RESTART;
		}

		return ADDON_STATUS_OK;
	}

	/***********************************************************
	* PVR Client AddOn specific public library functions
	***********************************************************/

	void OnSystemSleep()
	{
	}

	void OnSystemWake()
	{
	}

	void OnPowerSavingActivated()
	{
	}

	void OnPowerSavingDeactivated()
	{
	}

	PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
	{
		pCapabilities->bSupportsEPG					= true;
		pCapabilities->bSupportsTV					= true;
		pCapabilities->bSupportsRadio				= true;
		pCapabilities->bSupportsRecordings			= true;
		pCapabilities->bSupportsRecordingsUndelete	= false;
		pCapabilities->bSupportsTimers				= true;
		pCapabilities->bSupportsChannelGroups		= true;
		pCapabilities->bSupportsChannelScan			= false;
		pCapabilities->bHandlesInputStream			= true;
		pCapabilities->bHandlesDemuxing				= false;
		pCapabilities->bSupportsRecordingPlayCount	= true;
		pCapabilities->bSupportsLastPlayedPosition	= g_bEnableMultiResume;
		pCapabilities->bSupportsRecordingEdl		= false;
		pCapabilities->bSupportsRecordingsRename	= true;
		pCapabilities->bSupportsRecordingsLifetimeChange	= false;
		pCapabilities->bSupportsDescrambleInfo = false;

		return PVR_ERROR_NO_ERROR;
	}

	const char *GetBackendName(void)
	{
		static const char *strBackendName = "ServerWMC";
		return strBackendName;
	}

	const char *GetBackendVersion(void)
	{
		if (_wmc)
			return _wmc->GetBackendVersion();
		else
			return "0.0";
	}

	const char *GetConnectionString(void)
	{
		static std::string strConnectionString;
		strConnectionString = string_format("%s:%u", g_strServerName.c_str(), g_port);
		return strConnectionString.c_str();
	}

	const char *GetBackendHostname(void)
	{
		return g_strServerName.c_str();
	}

	PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
	{
		if (_wmc)
			return _wmc->GetDriveSpace(iTotal, iUsed);

		return PVR_ERROR_SERVER_ERROR;
	}

	PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
	{
		if (_wmc)
			return _wmc->GetEPGForChannel(handle, channel, iStart, iEnd);

		return PVR_ERROR_SERVER_ERROR;
	}


	PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
	{
		if (_wmc)
			return _wmc->SignalStatus(signalStatus);

		return PVR_ERROR_NO_ERROR;
	}

	// channel functions -----------------------------

	int GetChannelsAmount(void)
	{
		if (_wmc)
			return _wmc->GetChannelsAmount();

		return -1;
	}

	PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
	{
		if (_wmc)
			return _wmc->GetChannels(handle, bRadio);

		return PVR_ERROR_SERVER_ERROR;
	}

	int GetChannelGroupsAmount(void)
	{
		if (_wmc)
			return _wmc->GetChannelGroupsAmount();

		return -1;
	}

	PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
	{
		if (_wmc)
			return _wmc->GetChannelGroups(handle, bRadio);

		return PVR_ERROR_SERVER_ERROR;
	}

	PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
	{
		if (_wmc)
			return _wmc->GetChannelGroupMembers(handle, group);

		return PVR_ERROR_SERVER_ERROR;
	}


	// timer functions -----------------------

	PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
	{
		if (_wmc)
			return _wmc->GetTimerTypes(types, size);

		return PVR_ERROR_NOT_IMPLEMENTED;
	}

	int GetTimersAmount(void) 
	{ 
		if (_wmc)
			return _wmc->GetTimersAmount();

		return PVR_ERROR_SERVER_ERROR;
	}

	PVR_ERROR GetTimers(ADDON_HANDLE handle) 
	{ 
		if (_wmc)
			return _wmc->GetTimers(handle);

		return PVR_ERROR_SERVER_ERROR;
	}

	PVR_ERROR AddTimer(const PVR_TIMER &timer) 
	{ 
		if (_wmc)
			return _wmc->AddTimer(timer);

		return PVR_ERROR_NO_ERROR; 
	}

	PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
	{ 
		if (_wmc)
			return _wmc->AddTimer(timer);
		return PVR_ERROR_NO_ERROR;
	}

	PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
	{
		if (_wmc)
			return _wmc->DeleteTimer(timer, bForceDelete);
		return PVR_ERROR_NO_ERROR;
	}

	// recording file functions
	PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
	{ 
		if (!deleted && _wmc)
			return _wmc->GetRecordings(handle);
		return PVR_ERROR_NO_ERROR;
	}

	int GetRecordingsAmount(bool deleted)
	{ 
		if (!deleted && _wmc)
			return _wmc->GetRecordingsAmount();

		return -1;
	}

	PVR_ERROR RenameRecording(const PVR_RECORDING &recording) 
	{ 
		if (_wmc)
			return _wmc->RenameRecording(recording);
		return PVR_ERROR_NOT_IMPLEMENTED; 
	}


	// live stream functions ---------------------------

	bool OpenLiveStream(const PVR_CHANNEL &channel)
	{
		if (_wmc)
		{
			if (_wmc->OpenLiveStream(channel))
			{
				_bIsPlaying = true;
				return true;
			}
		}
		return false;
	}

	int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) 
	{ 
		if (_wmc)
		{
			return _wmc->ReadLiveStream(pBuffer, iBufferSize);
		}
		return -1;
	}

	void CloseLiveStream(void)
	{
		_bIsPlaying = false;
		if (_wmc)
		{
			_wmc->CloseLiveStream();
		}
	}

	long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) 
	{ 
		if (_wmc)
			return _wmc->SeekLiveStream(iPosition, iWhence);
		else
			return -1; 
	}

	long long PositionLiveStream(void) 
	{ 
		if (_wmc)
			return _wmc->PositionLiveStream();
		else
			return -1; 
	}

	long long LengthLiveStream(void) 
	{ 
		if (_wmc)
			return _wmc->LengthLiveStream();
		else
			return -1; 
	}

	void PauseStream(bool bPaused)
	{
		if (_wmc)
			return _wmc->PauseStream(bPaused);
	}

	bool CanPauseStream(void) 
	{
		return true; 
	}

	bool CanSeekStream(void) 
	{ 
		return true; 
	}

	// recorded stream functions -----------------------------

	bool OpenRecordedStream(const PVR_RECORDING &recording) 
	{ 
		if (_wmc)
		{
			CloseLiveStream();
			if (_wmc->OpenRecordedStream(recording))
			{
				_bIsPlaying = true;
				return true;
			}
		}
		return false; 
	}

	int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) 
	{ 
		if (_wmc)
		{
			return _wmc->ReadLiveStream(pBuffer, iBufferSize);
		}
		return -1;
	}

	void CloseRecordedStream(void) 
	{
		_bIsPlaying = false;
		if (_wmc)
		{
			_wmc->CloseLiveStream();
		}
	}

	long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */) 
	{ 
		if (_wmc)
			return _wmc->SeekLiveStream(iPosition, iWhence);
		else
			return -1; 
	}

	long long PositionRecordedStream(void) 
	{ 
		if (_wmc)
			return _wmc->PositionLiveStream();
		else
			return -1; 
	}

	long long LengthRecordedStream(void) 
	{ 
		if (_wmc)
			return _wmc->LengthLiveStream();
		else
			return -1; 
	}

	// recorded file functions -----------------

	PVR_ERROR DeleteRecording(const PVR_RECORDING &recording) 
	{ 
		if (_wmc)
			return _wmc->DeleteRecording(recording);
		return PVR_ERROR_NO_ERROR; 
	}


	PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition) 
	{ 
		if (_wmc && g_bEnableMultiResume)
			return _wmc->SetRecordingLastPlayedPosition(recording, lastplayedposition);
		return PVR_ERROR_NOT_IMPLEMENTED; 
	}

	int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording) 
	{ 
		if (_wmc && g_bEnableMultiResume)
			return _wmc->GetRecordingLastPlayedPosition(recording);
		return -1; 
	}

	PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count)
	{ 
		if (_wmc && g_bEnableMultiResume)
			return _wmc->SetRecordingPlayCount(recording, count);
		return PVR_ERROR_NOT_IMPLEMENTED; 
	}

	// time shift functions -----------------

	bool IsTimeshifting(void)
	{
		if (_wmc)
			return _wmc->IsTimeShifting();
		else
			return false;
	}
	time_t GetPlayingTime()
	{
		if (_wmc)
			return _wmc->GetPlayingTime();
		else
			return 0;
	}
	time_t GetBufferTimeStart()
	{
		if (_wmc)
			return _wmc->GetBufferTimeStart();
		else
			return 0;
	}
	time_t GetBufferTimeEnd()
	{
		if (_wmc)
			return _wmc->GetBufferTimeEnd();
		else
			return 0;
	}

	/** UNUSED API FUNCTIONS */
	PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR RenameChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR MoveChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel)  {  return PVR_ERROR_NOT_IMPLEMENTED;  }
	PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
	void DemuxReset(void) {}
	void DemuxFlush(void) {}
	void DemuxAbort(void) {}
	DemuxPacket* DemuxRead(void) { return NULL; }
	bool SeekTime(double,bool,double*) { return false; }
	void SetSpeed(int) {};
	bool IsRealTimeStream(void) { return true; }
	PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; };
	PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item) { return PVR_ERROR_NOT_IMPLEMENTED; }

	PVR_ERROR IsEPGTagPlayable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR IsEPGTagRecordable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES*) { return PVR_ERROR_NOT_IMPLEMENTED; }

	PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES* pProperties) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL* channel, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount) { return PVR_ERROR_NOT_IMPLEMENTED; }
	PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
}
