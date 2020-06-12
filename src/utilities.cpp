/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "utilities.h"

#include <cstdarg>
#include <kodi/Filesystem.h>
#include <stdio.h>
#include <string>

#define FORMAT_BLOCK_SIZE 2048 // # of bytes to increment per try

namespace Utils
{

// format related string functions taken from:
// http://www.flipcode.com/archives/Safe_sprintf.shtml

bool Str2Bool(const std::string& str)
{
  return str.compare("True") == 0 ? true : false;
}

std::vector<std::string> Split(const std::string& input,
                               const std::string& delimiter,
                               unsigned int iMaxStrings /* = 0 */)
{
  std::vector<std::string> results;
  if (input.empty())
    return results;

  size_t iPos = std::string::npos;
  size_t newPos = std::string::npos;
  size_t sizeS2 = delimiter.size();
  size_t isize = input.size();

  std::vector<unsigned int> positions;

  newPos = input.find(delimiter, 0);

  if (newPos == std::string::npos)
  {
    results.push_back(input);
    return results;
  }

  while (newPos != std::string::npos)
  {
    positions.push_back(newPos);
    iPos = newPos;
    newPos = input.find(delimiter, iPos + sizeS2);
  }

  // numFound is the number of delimiters which is one less
  // than the number of substrings
  unsigned int numFound = positions.size();
  if (iMaxStrings > 0 && numFound >= iMaxStrings)
    numFound = iMaxStrings - 1;

  for (unsigned int i = 0; i <= numFound; i++)
  {
    std::string s;
    if (i == 0)
    {
      if (i == numFound)
        s = input;
      else
        s = input.substr(i, positions[i]);
    }
    else
    {
      size_t offset = positions[i - 1] + sizeS2;
      if (offset < isize)
      {
        if (i == numFound)
          s = input.substr(offset);
        else if (i > 0)
          s = input.substr(positions[i - 1] + sizeS2, positions[i] - positions[i - 1] - sizeS2);
      }
    }
    results.push_back(s);
  }
  return results;
}

std::string Format(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  std::string str = FormatV(fmt, args);
  va_end(args);

  return str;
}

std::string FormatV(const char* fmt, va_list args)
{
  if (fmt == nullptr)
    return "";

  int size = FORMAT_BLOCK_SIZE;
  va_list argCopy;

  char* cstr = reinterpret_cast<char*>(malloc(sizeof(char) * size));
  if (cstr == nullptr)
    return "";

  while (1)
  {
    va_copy(argCopy, args);

    int nActual = vsnprintf(cstr, size, fmt, argCopy);
    va_end(argCopy);

    if (nActual > -1 && nActual < size) // We got a valid result
    {
      std::string str(cstr, nActual);
      free(cstr);
      return str;
    }
    if (nActual > -1) // Exactly what we will need (glibc 2.1)
      size = nActual + 1;
    else // Let's try to double the size (glibc 2.0)
      size *= 2;

    char* new_cstr = reinterpret_cast<char*>(realloc(cstr, sizeof(char) * size));
    if (new_cstr == nullptr)
    {
      free(cstr);
      return "";
    }

    cstr = new_cstr;
  }

  free(cstr);
  return "";
}

bool EndsWith(std::string const& fullString, std::string const& ending)
{
  if (fullString.length() >= ending.length())
  {
    return (0 ==
            fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
  }
  else
  {
    return false;
  }
}

bool StartsWith(std::string const& fullString, std::string const& starting)
{
  if (fullString.length() >= starting.length())
  {
    return (0 == fullString.compare(0, starting.length(), starting));
  }
  else
  {
    return false;
  }
}

// return the directory from the input file path
std::string GetDirectoryPath(std::string const& path)
{
  size_t found = path.find_last_of("/\\");
  if (found != std::string::npos)
    return path.substr(0, found);
  else
    return path;
}

bool ReadFileContents(std::string const& strFileName, std::string& strContent)
{
  kodi::vfs::CFile fileHandle;
  if (fileHandle.OpenFile(strFileName))
  {
    std::string buffer;
    while (fileHandle.ReadLine(buffer))
      strContent.append(buffer);
    return true;
  }
  return false;
}

bool WriteFileContents(std::string const& strFileName, const std::string& strContent)
{
  kodi::vfs::CFile fileHandle;
  if (fileHandle.OpenFileForWrite(strFileName, true))
  {
    int rc = fileHandle.Write(strContent.c_str(), strContent.length());
    if (rc)
    {
      kodi::Log(ADDON_LOG_DEBUG, "wrote file %s", strFileName.c_str());
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "can not write to %s", strFileName.c_str());
    }
    return rc >= 0;
  }
  return false;
}

} /* namespace Utils */
