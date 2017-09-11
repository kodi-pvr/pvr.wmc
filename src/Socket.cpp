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

#include "libXBMC_addon.h"
#include <string>
#include "p8-platform/os.h"
#include "client.h"
#include "Socket.h"

#include "utilities.h"
#include "p8-platform/util/timeutils.h"
#include "p8-platform/threads/mutex.h"

using namespace std;
using namespace ADDON;

P8PLATFORM::CMutex        m_mutex;

/* Master defines for client control */
//#define RECEIVE_TIMEOUT 6 //sec

Socket::Socket(const enum SocketFamily family, const enum SocketDomain domain, const enum SocketType type, const enum SocketProtocol protocol)
{
	_sd = INVALID_SOCKET;
	_family = family;
	_domain = domain;
	_type = type;
	_protocol = protocol;
	memset (&_sockaddr, 0, sizeof( _sockaddr ) );
	//set_non_blocking(1);  
}

Socket::Socket()
{
	// Default constructor, default settings
	_sd = INVALID_SOCKET;
	_family = af_inet;
	_domain = pf_inet;
	_type = sock_stream;
	_protocol = tcp;
	memset (&_sockaddr, 0, sizeof( _sockaddr ) );
}

Socket::~Socket()
{
	close();
}

bool Socket::setHostname ( const std::string& host )
{
	if (isalpha(host.c_str()[0]))
	{
		// host address is a name
		struct hostent *he = NULL;
		if ((he = gethostbyname( host.c_str() )) == 0)
		{
			errormessage( getLastError(), "Socket::setHostname");
			return false;
		}

		_sockaddr.sin_addr = *((in_addr *) he->h_addr);
	}
	else
	{
		_sockaddr.sin_addr.s_addr = inet_addr(host.c_str());
	}
	return true;
}

bool Socket::close()
{
	if (is_valid())
	{
		if (_sd != SOCKET_ERROR)
#ifdef TARGET_WINDOWS
			closesocket(_sd);
#else
			::close(_sd);
#endif
		_sd = INVALID_SOCKET;
		osCleanup();
		return true;
	}
	return false;
}

int _timeout = 0;

bool Socket::create()
{
	if( is_valid() )
	{
		close();
	}

	if(!osInit())
	{
		return false;
	}

	_sd = socket(_family, _type, _protocol );
	//0 indicates that the default protocol for the type selected is to be used.
	//For example, IPPROTO_TCP is chosen for the protocol if the type  was set to
	//SOCK_STREAM and the address family is AF_INET.

	if (_sd == INVALID_SOCKET)
	{
		errormessage( getLastError(), "Socket::create" );
		return false;
	}

	// if requested set a timeout for receiving data
	if (_timeout != 0)
	{
		struct timeval tv;

		tv.tv_sec = _timeout;	// set the receive timeout desired
		tv.tv_usec = 0;			// Not init'ing this can cause strange errors
		setsockopt(_sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv,sizeof(struct timeval));	// set a receive timeout for this new socket
		_timeout = 0;			// reset to default state of no timeout
	}

	return true;
}

int Socket::send( const std::string& data )
{
	if (!is_valid())
	{
		return 0;
	}

	int status = Socket::send( (const char*) data.c_str(), (const unsigned int) data.size());

	return status;
}

int Socket::send ( const char* data, const unsigned int len )
{
	fd_set set_w, set_e;
	struct timeval tv;
	int  result;

	if (!is_valid())
	{
		return 0;
	}

	// fill with new data
	tv.tv_sec  = 0;
	tv.tv_usec = 0;

	FD_ZERO(&set_w);
	FD_ZERO(&set_e);
	FD_SET(_sd, &set_w);
	FD_SET(_sd, &set_e);

	result = select(FD_SETSIZE, &set_w, NULL, &set_e, &tv);

	if (result < 0)
	{
		XBMC->Log(LOG_ERROR, "Socket::send  - select failed");
		_sd = INVALID_SOCKET;
		return 0;
	}
	if (FD_ISSET(_sd, &set_w))
	{
		XBMC->Log(LOG_ERROR, "Socket::send  - failed to send data");
		_sd = INVALID_SOCKET;
		return 0;
	}

	int status = ::send(_sd, data, len, 0 );

	if (status == -1)
	{
		errormessage( getLastError(), "Socket::send");
		XBMC->Log(LOG_ERROR, "Socket::send  - failed to send data");
		_sd = INVALID_SOCKET;
	}
	return status;
}

//Receive until error or \n
bool Socket::ReadResponses(int &code, vector<std::string> &lines)
{
	int					result;
	char				buffer[4096];		// this buff size has to be known in server
	code = 0;
	
	bool readComplete = false;
	std::string bigString = "";

	do
	{
		result = recv(_sd, buffer, sizeof(buffer) - 1, 0);
		if (result < 0)								// if result is negative, the socket is bad
		{
#ifdef TARGET_WINDOWS
			int errorCode = WSAGetLastError();
			XBMC->Log(LOG_DEBUG, "ReadResponse ERROR - recv failed, Err: %d", errorCode);
#else
			XBMC->Log(LOG_DEBUG, "ReadResponse ERROR - recv failed");
#endif
			code = 1; 
			_sd = INVALID_SOCKET;
			return false;
		} 

		if (result > 0)								// if we got data from the server in this last pass
		{
			buffer[result] = 0;						// insert end of string marker
			bigString.append(buffer);				// accumulate all the reads
		}

	} while (result > 0);							// keep reading until result returns '0', meaning server is done sending reponses

	if (EndsWith(bigString, "<EOF>"))
	{
		readComplete = true;						// all server data has benn read
		lines = split(bigString, "<EOL>");			// split each reponse by <EOL> delimiters
		lines.erase(lines.end() - 1);				// erase <EOF> at end
	}
	else
	{
		XBMC->Log(LOG_DEBUG, "ReadResponse ERROR - <EOF> in read reponses not found");
		_sd = INVALID_SOCKET;
	}

	
	return readComplete;
}

bool Socket::connect ( const std::string& host, const unsigned short port )
{
	if ( !is_valid() )
	{
		return false;
	}

	_sockaddr.sin_family = (sa_family_t) _family;
	_sockaddr.sin_port = htons ( port );

	if ( !setHostname( host ) )
	{
		XBMC->Log(LOG_ERROR, "Socket::setHostname(%s) failed.\n", host.c_str());
		return false;
	}

	int status = ::connect ( _sd, reinterpret_cast<sockaddr*>(&_sockaddr), sizeof ( _sockaddr ) );

	if ( status == SOCKET_ERROR )
	{
		XBMC->Log(LOG_ERROR, "Socket::connect %s:%u\n", host.c_str(), port);
		errormessage( getLastError(), "Socket::connect" );
		return false;
	}

	return true;
}

bool Socket::reconnect()
{
	if ( _sd != INVALID_SOCKET )
	{
		return true;
	}

	if( !create() )
		return false;

	int status = ::connect ( _sd, reinterpret_cast<sockaddr*>(&_sockaddr), sizeof ( _sockaddr ) );

	if ( status == SOCKET_ERROR )
	{
		errormessage( getLastError(), "Socket::connect" );
		return false;
	}

	return true;
}

bool Socket::is_valid() const
{
	return (_sd != INVALID_SOCKET);
}

#if defined(TARGET_WINDOWS)
bool Socket::set_non_blocking ( const bool b )
{
	u_long iMode;

	if ( b )
		iMode = 1;  // enable non_blocking
	else
		iMode = 0;  // disable non_blocking

	if (ioctlsocket(_sd, FIONBIO, &iMode) == -1)
	{
		XBMC->Log(LOG_ERROR, "Socket::set_non_blocking - Can't set socket condition to: %i", iMode);
		return false;
	}

	return true;
}

void Socket::errormessage( int errnum, const char* functionname) const
{
	const char* errmsg = NULL;

	switch (errnum)
	{
	case WSANOTINITIALISED:
		errmsg = "A successful WSAStartup call must occur before using this function.";
		break;
	case WSAENETDOWN:
		errmsg = "The network subsystem or the associated service provider has failed";
		break;
	case WSA_NOT_ENOUGH_MEMORY:
		errmsg = "Insufficient memory available";
		break;
	case WSA_INVALID_PARAMETER:
		errmsg = "One or more parameters are invalid";
		break;
	case WSA_OPERATION_ABORTED:
		errmsg = "Overlapped operation aborted";
		break;
	case WSAEINTR:
		errmsg = "Interrupted function call";
		break;
	case WSAEBADF:
		errmsg = "File handle is not valid";
		break;
	case WSAEACCES:
		errmsg = "Permission denied";
		break;
	case WSAEFAULT:
		errmsg = "Bad address";
		break;
	case WSAEINVAL:
		errmsg = "Invalid argument";
		break;
	case WSAENOTSOCK:
		errmsg = "Socket operation on nonsocket";
		break;
	case WSAEDESTADDRREQ:
		errmsg = "Destination address required";
		break;
	case WSAEMSGSIZE:
		errmsg = "Message too long";
		break;
	case WSAEPROTOTYPE:
		errmsg = "Protocol wrong type for socket";
		break;
	case WSAENOPROTOOPT:
		errmsg = "Bad protocol option";
		break;
	case WSAEPFNOSUPPORT:
		errmsg = "Protocol family not supported";
		break;
	case WSAEAFNOSUPPORT:
		errmsg = "Address family not supported by protocol family";
		break;
	case WSAEADDRINUSE:
		errmsg = "Address already in use";
		break;
	case WSAECONNRESET:
		errmsg = "Connection reset by peer";
		break;
	case WSAHOST_NOT_FOUND:
		errmsg = "Authoritative answer host not found";
		break;
	case WSATRY_AGAIN:
		errmsg = "Nonauthoritative host not found, or server failure";
		break;
	case WSAEISCONN:
		errmsg = "Socket is already connected";
		break;
	case WSAETIMEDOUT:
		errmsg = "Connection timed out";
		break;
	case WSAECONNREFUSED:
		errmsg = "Connection refused";
		break;
	case WSANO_DATA:
		errmsg = "Valid name, no data record of requested type";
		break;
	default:
		errmsg = "WSA Error";
	}
	XBMC->Log(LOG_ERROR, "%s: (Winsock error=%i) %s\n", functionname, errnum, errmsg);
}

int Socket::getLastError() const
{
	return WSAGetLastError();
}

int Socket::win_usage_count = 0; //Declared static in Socket class

bool Socket::osInit()
{
	win_usage_count++;
	// initialize winsock:
	if (WSAStartup(MAKEWORD(2,2),&_wsaData) != 0)
	{
		return false;
	}

	WORD wVersionRequested = MAKEWORD(2,2);

	// check version
	if (_wsaData.wVersion != wVersionRequested)
	{
		return false;
	}

	return true;
}

void Socket::osCleanup()
{
	win_usage_count--;
	if(win_usage_count == 0)
	{
		WSACleanup();
	}
}

#elif defined TARGET_LINUX || defined TARGET_DARWIN || defined TARGET_FREEBSD
bool Socket::set_non_blocking ( const bool b )
{
	int opts;

	opts = fcntl(_sd, F_GETFL);

	if ( opts < 0 )
	{
		return false;
	}

	if ( b )
		opts = ( opts | O_NONBLOCK );
	else
		opts = ( opts & ~O_NONBLOCK );

	if(fcntl (_sd , F_SETFL, opts) == -1)
	{
		XBMC->Log(LOG_ERROR, "Socket::set_non_blocking - Can't set socket flags to: %i", opts);
		return false;
	}
	return true;
}

void Socket::errormessage( int errnum, const char* functionname) const
{
	const char* errmsg = NULL;

	switch ( errnum )
	{
	case EAGAIN: //same as EWOULDBLOCK
		errmsg = "EAGAIN: The socket is marked non-blocking and the requested operation would block";
		break;
	case EBADF:
		errmsg = "EBADF: An invalid descriptor was specified";
		break;
	case ECONNRESET:
		errmsg = "ECONNRESET: Connection reset by peer";
		break;
	case EDESTADDRREQ:
		errmsg = "EDESTADDRREQ: The socket is not in connection mode and no peer address is set";
		break;
	case EFAULT:
		errmsg = "EFAULT: An invalid userspace address was specified for a parameter";
		break;
	case EINTR:
		errmsg = "EINTR: A signal occurred before data was transmitted";
		break;
	case EINVAL:
		errmsg = "EINVAL: Invalid argument passed";
		break;
	case ENOTSOCK:
		errmsg = "ENOTSOCK: The argument is not a valid socket";
		break;
	case EMSGSIZE:
		errmsg = "EMSGSIZE: The socket requires that message be sent atomically, and the size of the message to be sent made this impossible";
		break;
	case ENOBUFS:
		errmsg = "ENOBUFS: The output queue for a network interface was full";
		break;
	case ENOMEM:
		errmsg = "ENOMEM: No memory available";
		break;
	case EPIPE:
		errmsg = "EPIPE: The local end has been shut down on a connection oriented socket";
		break;
	case EPROTONOSUPPORT:
		errmsg = "EPROTONOSUPPORT: The protocol type or the specified protocol is not supported within this domain";
		break;
	case EAFNOSUPPORT:
		errmsg = "EAFNOSUPPORT: The implementation does not support the specified address family";
		break;
	case ENFILE:
		errmsg = "ENFILE: Not enough kernel memory to allocate a new socket structure";
		break;
	case EMFILE:
		errmsg = "EMFILE: Process file table overflow";
		break;
	case EACCES:
		errmsg = "EACCES: Permission to create a socket of the specified type and/or protocol is denied";
		break;
	case ECONNREFUSED:
		errmsg = "ECONNREFUSED: A remote host refused to allow the network connection (typically because it is not running the requested service)";
		break;
	case ENOTCONN:
		errmsg = "ENOTCONN: The socket is associated with a connection-oriented protocol and has not been connected";
		break;
		//case E:
		//	errmsg = "";
		//	break;
	default:
		break;
	}
	XBMC->Log(LOG_ERROR, "%s: (errno=%i) %s\n", functionname, errnum, errmsg);
}

int Socket::getLastError() const
{
	return errno;
}

bool Socket::osInit()
{
	// Not needed for Linux
	return true;
}

void Socket::osCleanup()
{
	// Not needed for Linux
}
#endif //TARGET_WINDOWS || TARGET_LINUX || TARGET_DARWIN || TARGET_FREEBSD


void Socket::SetServerName(std::string strServerName)
{
	_serverName = strServerName;
}

void Socket::SetClientName(std::string strClientName)
{
	_clientName = strClientName;
}

void Socket::SetServerPort(int port)
{
	_port = port;
}

int Socket::SendRequest(std::string requestStr)
{
	std::string sRequest;		
	sRequest = string_format("%s|%s<Client Quit>", _clientName.c_str(), requestStr.c_str());	// build the request string
	int status = send(sRequest);
	return status;
}

// set a timeout for the next socket operation called, otherwise no timout is set
void Socket::SetTimeOut(int tSec)
{
	_timeout = tSec;
}

std::vector<std::string> Socket::GetVector(const std::string &request, bool allowRetry, bool allowWOL /* = true*/)
{
	int maxAttempts = 3;
	int sleepAttemptsMs = 1000;

	P8PLATFORM::CLockObject lock(m_mutex);						// only process one request at a time

	int code;
	std::vector<std::string> reponses;

	int cntAttempts = 1;
	while (cntAttempts <= maxAttempts)
	{
		XBMC->Log(LOG_DEBUG, "Socket::GetVector> Send request \"%s\"", request.c_str());
		reponses.clear();

		if (!create())												// create the socket
		{
			XBMC->Log(LOG_ERROR, "Socket::GetVector> error could not create socket");
			reponses.push_back("SocketError");						// set a SocketError message (not fatal)
		}
		else														// socket created OK
		{
			// Attempt Wake On Lan
			if (g_BackendOnline != BACKEND_UP && allowWOL && g_bWakeOnLAN && g_strServerMAC != "")
			{
				XBMC->Log(LOG_INFO, "Socket::GetVector> Sending WOL packet to %s", g_strServerMAC.c_str());
				if (g_BackendOnline != BACKEND_UNKNOWN)
				{
					std::string infoStr = XBMC->GetLocalizedString(30026);		
					XBMC->QueueNotification(QUEUE_INFO, infoStr.c_str());	// Notify WOL is being sent
				}
				XBMC->WakeOnLan(g_strServerMAC.c_str());						// Send WOL request
			}

			if (!connect(_serverName, (unsigned short)_port))	// if this fails, it is likely due to server down
			{
				// Failed to connect
				g_BackendOnline = BACKEND_DOWN;
				XBMC->Log(LOG_ERROR, "Socket::GetVector> Server is down");
				reponses.push_back("ServerDown");					// set a server down error message (not fatal)
			}
			else
			{
				// Connected OK
				g_BackendOnline = BACKEND_UP;
				int bytesSent = SendRequest(request.c_str());		// send request to server

				if (bytesSent > 0)									// if request was sent successfully
				{
					if (!ReadResponses(code, reponses))
					{
						XBMC->Log(LOG_ERROR, "Socket::GetVector> error getting responses");
						reponses.clear();
						reponses.push_back("SocketError");			
					}
					else
					{
						break;
					}
				}
				else												// error sending request
				{
					XBMC->Log(LOG_ERROR, "Socket::GetVector> error sending server request");
					reponses.push_back("SocketError");			
				}
			}
		}

		if (!allowRetry)
		{
			break;
		}

		cntAttempts++;
		XBMC->Log(LOG_DEBUG, "Socket::GetVector> Retrying in %ims", sleepAttemptsMs);
		usleep(sleepAttemptsMs * 1000);
	}

	close();													// close socket
	return reponses;											// return responses
}

std::string Socket::GetString(const std::string &request, bool allowRetry, bool allowWOL /* = true*/)
{
	std::vector<std::string> result = GetVector(request, allowRetry, allowWOL);
	return result[0];
}

bool Socket::GetBool(const std::string &request, bool allowRetry, bool allowWOL /* = true*/)
{
	return GetString(request, allowRetry, allowWOL) == "True";
}


int Socket::GetInt(const std::string &request, bool allowRetry, bool allowWOL /* = true*/)
{
	std::string valStr = GetString(request, allowRetry, allowWOL);
	long val = strtol(valStr.c_str(), 0, 10);
	return val;
}

long long Socket::GetLL(const std::string &request, bool allowRetry, bool allowWOL /* = true*/)
{
	std::string valStr = GetString(request, allowRetry, allowWOL);
	long long val = strtoll(valStr.c_str(), NULL, 10);
	return val;
}
