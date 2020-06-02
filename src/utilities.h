/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "stdio.h"

#include <string>
#include <vector>

//std::vector<std::string> split(const std::string& s, const std::string& delim);

bool Str2Bool(const std::string& str);

bool EndsWith(std::string const& fullString, std::string const& ending);
bool StartsWith(std::string const& fullString, std::string const& starting);
std::string GetDirectoryPath(std::string const& path);
bool ReadFileContents(std::string const& strFileName, std::string& strResult);
bool WriteFileContents(std::string const& strFileName, std::string& strContent);
