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

#include <string>
#include <vector>

class CDSFile
{
public:
  /*!
   * \brief What kind of disc a path holds
   *
   * A rule in mediasconfig.xml is chosen by extension, and "iso" says nothing about which
   * kind of disc is inside. A DVD image handed to the Blu-ray source dies at "error opening
   * file BDMV\\index.bdmv" with nothing on screen, so the two have to be told apart by
   * looking rather than by name.
   */
  enum class DiscType
  {
    None, //!< Not a disc: an ordinary media file, or an image of something else
    Bluray,
    Dvd
  };

  /*!
   * \brief Which kind of disc is at this path, by looking inside it
   * \param strFileName A disc image, a file within a disc folder, or anything else
   *
   * Answers by the same test Kodi's own CDVDFactoryInputStream uses: an image holding
   * BDMV/index.bdmv is a Blu-ray, one holding VIDEO_TS/VIDEO_TS.IFO is a DVD. Disc folders
   * are recognised from the path alone, since the names are right there in it.
   *
   * The answer is cached for the path last asked about. Rule matching asks once per
   * candidate rule, and each miss otherwise costs opening the image again -- over a network
   * share, for a file that has not changed between two questions a millisecond apart.
   */
  static DiscType DiscTypeOf(const std::string& strFileName);

  static std::string SmbToUncPath(const std::string& strFileName);
  static bool Exists(const std::string& strFileName, long* errCode = NULL);

  /*!
   * \brief The two folders an unpacked disc's own files sit in
   * \param strFileName A file inside a disc folder, or the BDMV/VIDEO_TS folder itself
   * \param root Receives the folder BDMV or VIDEO_TS sits in
   * \param structure Receives the BDMV or VIDEO_TS folder itself
   * \return false when the path is not part of a disc folder at all, leaving both untouched
   *
   * Both are wanted because "beside the disc" has two readings and a viewer may have meant
   * either: beside the folder the disc was unpacked into, or beside the index file that was
   * actually chosen to play it. Answering only one of them would be a guess.
   */
  static bool DiscFolderRoot(const std::string& strFileName,
                             std::string& root,
                             std::string& structure);

  /*!
   * \brief Subtitle files that belong to what is about to be played, disc or not
   *
   * Kodi's own CUtil::ScanForExternalSubtitles is the whole answer for an ordinary file and
   * for a disc image, both of which sit beside their subtitles under one name --
   * "Film.srt", "Film.en.srt", "Film.pt.srt" next to "Film.iso".
   *
   * An unpacked disc is the awkward case, and it is why this exists. A disc folder is played
   * by choosing a file buried inside it -- BDMV/index.bdmv, VIDEO_TS/VIDEO_TS.IFO -- so that
   * scan goes looking for files called "index.srt" or "VIDEO_TS.srt", which nobody names one.
   * The right directory is the disc's own folder, and since a disc folder holds exactly one
   * disc, every subtitle file in it belongs to that disc whatever it is called. That is the
   * whole difference from a folder of media files, where a name has to match to mean anything.
   */
  static void ScanForSubtitles(const std::string& strFileName,
                               std::vector<std::string>& subtitles);

  /*!
   * \brief The name a subtitle file beside this one would be named after
   *
   * "Film.en.srt" is English because "Film" is what the video is called and "en" is what is
   * left. For an unpacked disc the file being played is called index.bdmv or VIDEO_TS.IFO
   * and nothing is named after that, so what is left of "Film.en.srt" is the whole of it and
   * the language is lost. The disc's own folder carries the name a viewer would use, so it
   * is what the parse is given, and a plain "Film.srt" beside a disc folder comes out with
   * nothing left over -- which is the unlabelled subtitle that must still be offered.
   *
   * Anything that is not a disc folder is returned unchanged.
   */
  static std::string SubtitleNameBase(const std::string& strFileName);

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
