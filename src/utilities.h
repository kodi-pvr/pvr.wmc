/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "stdio.h"

#include <cstdarg>
#include <string>
#include <vector>

namespace Utils
{

std::vector<std::string> Split(const std::string& input,
                               const std::string& delimiter,
                               unsigned int iMaxStrings = 0);
bool Str2Bool(const std::string& str);
std::string Format(const char* fmt, ...);
std::string FormatV(const char* fmt, va_list args);
bool EndsWith(std::string const& fullString, std::string const& ending);
bool StartsWith(std::string const& fullString, std::string const& starting);
std::string GetDirectoryPath(std::string const& path);
bool ReadFileContents(std::string const& strFileName, std::string& strResult);
bool WriteFileContents(std::string const& strFileName, const std::string& strContent);

} /* namespace Utils */
