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
#include "utilities.h"

#include "client.h"
#include <string>
#include <stdio.h>
#include <cstdarg>

using namespace ADDON;

// format related string functions taken from:
// http://www.flipcode.com/archives/Safe_sprintf.shtml

bool Str2Bool(const std::string& str)
{
	return str.compare("True") == 0 ? true:false;
}

bool EndsWith(std::string const &fullString, std::string const &ending)
{
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

bool StartsWith(std::string const &fullString, std::string const &starting)
{
    if (fullString.length() >= starting.length()) {
        return (0 == fullString.compare(0, starting.length(), starting));
    } else {
        return false;
    }
}

// return the directory from the input file path
std::string GetDirectoryPath(std::string const &path)
{
	size_t found = path.find_last_of("/\\");
	if (found != std::string::npos)
		return path.substr(0, found);
	else
		return path;
}

bool ReadFileContents(std::string const &strFileName, std::string &strContent)
{
	void* fileHandle = XBMC->OpenFile(strFileName.c_str(), 0);
	if (fileHandle)
	{
		char buffer[1024];
		while (XBMC->ReadFileString(fileHandle, buffer, 1024))
			strContent.append(buffer);
		XBMC->CloseFile(fileHandle);
		return true;
	}
	return false;
}

bool WriteFileContents(std::string const &strFileName, std::string &strContent)
{
	void* fileHandle = XBMC->OpenFileForWrite(strFileName.c_str(), true);
	if (fileHandle)
	{
		int rc = XBMC->WriteFile(fileHandle, strContent.c_str(), strContent.length());
		if (rc)
		{
			XBMC->Log(LOG_DEBUG, "wrote file %s", strFileName.c_str());
		}
		else
		{
			XBMC->Log(LOG_ERROR, "can not write to %s", strFileName.c_str());
		}
		XBMC->CloseFile(fileHandle);
		return rc >= 0;
	}
	return false;
}