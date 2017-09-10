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

#include "p8-platform/util/StdString.h"
#include "util/XMLUtils.h"
#include "utilities.h"

#include "client.h"
#include <string>
#include <stdio.h>
#include <cstdarg>
#ifdef TARGET_WINDOWS
    #include <windows.h>
#else
    #include <stdarg.h>
#endif

using namespace ADDON;

// format related string functions taken from:
// http://www.flipcode.com/archives/Safe_sprintf.shtml

bool Str2Bool(const CStdString str)
{
	return str.compare("True") == 0 ? true:false;
}

std::vector<CStdString> split(const CStdString& s, const CStdString& delim, const bool keep_empty) {
    std::vector<CStdString> result;
    if (delim.empty()) {
        result.push_back(s);
        return result;
    }
    CStdString::const_iterator substart = s.begin(), subend;
    while (true) {
        subend = search(substart, s.end(), delim.begin(), delim.end());
        CStdString temp(substart, subend);
        if (keep_empty || !temp.empty()) {
            result.push_back(temp);
        }
        if (subend == s.end()) {
            break;
        }
        substart = subend + delim.size();
    }
    return result;
}

bool EndsWith(CStdString const &fullString, CStdString const &ending)
{
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

bool StartsWith(CStdString const &fullString, CStdString const &starting)
{
    if (fullString.length() >= starting.length()) {
        return (0 == fullString.compare(0, starting.length(), starting));
    } else {
        return false;
    }
}

// return the directory from the input file path
CStdString GetDirectoryPath(CStdString const &path)
{
	size_t found = path.find_last_of("/\\");
	if (found != CStdString::npos)
		return path.substr(0, found);
	else
		return path;
}

bool ReadFileContents(CStdString const &strFileName, CStdString &strContent)
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

bool WriteFileContents(CStdString const &strFileName, CStdString &strContent)
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
