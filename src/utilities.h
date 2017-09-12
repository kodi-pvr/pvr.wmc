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
#include <string>
#include "stdio.h"

//std::vector<std::string> split(const std::string& s, const std::string& delim);

bool Str2Bool(const std::string& str);

bool EndsWith(std::string const &fullString, std::string const &ending);
bool StartsWith(std::string const &fullString, std::string const &starting);
std::string GetDirectoryPath(std::string const &path);
bool ReadFileContents(std::string const &strFileName, std::string &strResult);
bool WriteFileContents(std::string const &strFileName, std::string &strContent);

