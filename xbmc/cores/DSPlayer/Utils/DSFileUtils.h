#pragma once
/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://www.xbmc.org
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _DSFILEUTILS_H
#define _DSFILEUTILS_H


#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif

#include "utils/XBMCTinyXML.h"

class CDSFile
{
public:
  static std::string SmbToUncPath(const std::string& strFileName);
  static bool Exists(const std::string& strFileName, long* errCode = NULL);

  /*!
   * \brief The path libbluray's bd_open() wants for the disc a file belongs to
   *
   * A disc image names the disc itself, but a disc folder is played by picking a file out of
   * it -- BDMV/index.bdmv from the file list, or a playlist under BDMV/PLAYLIST -- and
   * bd_open() wants the folder BDMV sits in. Handed the file it opens nothing at all.
   *
   * Anything that is not part of a disc folder is returned unchanged, so this is safe to
   * apply to every path a disc might arrive as.
   */
  static std::string BlurayDiscRoot(const std::string& strFileName);
};

class CDSCharsetConverter
{
public:
  static int getCharsetIdByName(const std::string& charsetName);
};

class CDSTimeUtils
{
public:
  static int64_t GetPerfCounter();
};


class CDSXMLUtils
{
public:
  static bool GetInt(TiXmlElement *pElement, const std::string &attr, int *iValue);
  static bool GetFloat(TiXmlElement *pElement, const std::string &attr, float *fValue);
  static bool GetString(TiXmlElement *pElement, const std::string &attr, std::string *sValue);
  static std::string GetString(TiXmlElement *pElement, const std::string &attr);
  static bool GetTristate(TiXmlElement *pElement, const std::string &attr, int *iValue);
};
#endif
