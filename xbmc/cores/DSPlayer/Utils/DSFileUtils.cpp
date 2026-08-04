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
#include "utils/DSFileUtils.h"
#include "DSUtil/DSUtil.h"
#include "FileItem.h"
#include "FileItemList.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "threads/CriticalSection.h"
#include "utils/charsetconverter.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

#include <cstring>
#include <vector>

#if HAS_DS_PLAYER

static struct SDSCharsetMapping
{
  const char* charset;
  const char* caption;
  int win_id;
} g_charsets[] = {
  { "ISO-8859-1", "Western Europe (ISO)", DEFAULT_CHARSET }
  , { "ISO-8859-2", "Central Europe (ISO)", DEFAULT_CHARSET }
  , { "ISO-8859-3", "South Europe (ISO)", DEFAULT_CHARSET }
  , { "ISO-8859-4", "Baltic (ISO)", DEFAULT_CHARSET }
  , { "ISO-8859-5", "Cyrillic (ISO)", DEFAULT_CHARSET }
  , { "ISO-8859-6", "Arabic (ISO)", DEFAULT_CHARSET }
  , { "ISO-8859-7", "Greek (ISO)", DEFAULT_CHARSET }
  , { "ISO-8859-8", "Hebrew (ISO)", DEFAULT_CHARSET }
  , { "ISO-8859-9", "Turkish (ISO)", DEFAULT_CHARSET }
  , { "CP1250", "Central Europe (Windows)", EASTEUROPE_CHARSET }
  , { "CP1251", "Cyrillic (Windows)", RUSSIAN_CHARSET }
  , { "CP1252", "Western Europe (Windows)", ANSI_CHARSET }
  , { "CP1253", "Greek (Windows)", GREEK_CHARSET }
  , { "CP1254", "Turkish (Windows)", TURKISH_CHARSET }
  , { "CP1255", "Hebrew (Windows)", HEBREW_CHARSET }
  , { "CP1256", "Arabic (Windows)", ARABIC_CHARSET }
  , { "CP1257", "Baltic (Windows)", BALTIC_CHARSET }
  , { "CP1258", "Vietnamesse (Windows)", VIETNAMESE_CHARSET }
  , { "CP874", "Thai (Windows)", THAI_CHARSET }
  , { "BIG5", "Chinese Traditional (Big5)", CHINESEBIG5_CHARSET }
  , { "GBK", "Chinese Simplified (GBK)", GB2312_CHARSET }
  , { "SHIFT_JIS", "Japanese (Shift-JIS)", SHIFTJIS_CHARSET }
  , { "CP949", "Korean", HANGEUL_CHARSET }
  , { "BIG5-HKSCS", "Hong Kong (Big5-HKSCS)", DEFAULT_CHARSET }
  , { NULL, NULL, 0 }
};


namespace
{
//! Answer for the path last asked about, see CDSFile::DiscTypeOf
CCriticalSection g_discTypeLock;
std::string g_discTypePath;
CDSFile::DiscType g_discTypeAnswer{CDSFile::DiscType::None};

/*!
 * \brief Whether an image's ISO9660 root directory holds a folder of the given name
 *
 * Read straight out of the image with an ordinary file handle, so that an image Kodi cannot
 * browse is still routed to the right player.
 *
 * Kodi browses a disc image with udfread, and some rips carry no UDF file system at all --
 * one of the discs here has only ISO9660, with none of the BEA01/NSR02/TEA01 bridge
 * descriptors and no anchor at sector 256 -- so browsing it returns nothing whatsoever. A
 * DVD-Video image is meant to be UDF and libdvdread will not play one that is not, but
 * getting the disc as far as libdvdread's own complaint is worth far more than the graph
 * failing earlier with "Extension iso not found", which says nothing about the disc.
 *
 * Every properly made DVD-Video and Blu-ray image is written in the UDF bridge format, an
 * ISO9660 file system describing the same tree alongside the UDF one, so the folder that
 * says which kind of disc this is can be found without a UDF reader either way.
 */
bool Iso9660HoldsFolder(const std::string& image, const std::string& folder)
{
  constexpr int64_t SECTOR = 2048;
  //! ISO9660 puts its first volume descriptor here, after the 32 kB system area
  constexpr int64_t PRIMARY_VOLUME_DESCRIPTOR = 16 * SECTOR;
  //! Where the root directory's own record sits inside that descriptor
  constexpr size_t ROOT_RECORD = 156;

  XFILE::CFile file;
  if (!file.Open(image))
  {
    CLog::Log(LOGDEBUG, "{} - cannot open \"{}\" to read its file system", __FUNCTION__,
              image.c_str());
    return false;
  }

  std::vector<uint8_t> sector(SECTOR);
  if (file.Seek(PRIMARY_VOLUME_DESCRIPTOR, SEEK_SET) != PRIMARY_VOLUME_DESCRIPTOR ||
      file.Read(sector.data(), SECTOR) != SECTOR)
  {
    CLog::Log(LOGDEBUG, "{} - cannot read the volume descriptor of \"{}\"", __FUNCTION__,
              image.c_str());
    return false;
  }

  // Type 1 is the primary volume descriptor, and CD001 is the standard identifier every
  // ISO9660 volume carries
  if (sector[0] != 1 || std::memcmp(&sector[1], "CD001", 5) != 0)
  {
    CLog::Log(LOGDEBUG, "{} - \"{}\" has no ISO9660 volume descriptor", __FUNCTION__,
              image.c_str());
    return false;
  }

  // Little endian halves of the root directory's extent and length. Both are stored twice,
  // once in each byte order; the little endian copy comes first.
  const uint8_t* root = &sector[ROOT_RECORD];
  const uint32_t extent = root[2] | (root[3] << 8) | (root[4] << 16) | (root[5] << 24);
  const uint32_t length = root[10] | (root[11] << 8) | (root[12] << 16) | (root[13] << 24);
  if (length == 0 || length > 1024 * 1024)
    return false;

  std::vector<uint8_t> directory(length);
  const int64_t at = static_cast<int64_t>(extent) * SECTOR;
  if (file.Seek(at, SEEK_SET) != at ||
      file.Read(directory.data(), length) != static_cast<ssize_t>(length))
    return false;

  // Walk the records. A zero length means the rest of this sector is padding, so step to the
  // next one rather than stopping: a directory may span several.
  for (size_t i = 0; i + 33 < length;)
  {
    const uint8_t recordLength = directory[i];
    if (recordLength == 0)
    {
      i = ((i / SECTOR) + 1) * SECTOR;
      continue;
    }

    const uint8_t nameLength = directory[i + 32];
    if (nameLength > 0 && i + 33 + nameLength <= length)
    {
      std::string name(reinterpret_cast<const char*>(&directory[i + 33]), nameLength);
      // A file carries a ";1" version suffix, a directory does not, and either way the name
      // we are looking for is the part before it
      const size_t version = name.find(';');
      if (version != std::string::npos)
        name.resize(version);

      if (StringUtils::EqualsNoCase(name, folder))
      {
        CLog::Log(LOGDEBUG, "{} - found {} by reading \"{}\" directly", __FUNCTION__,
                  folder.c_str(), image.c_str());
        return true;
      }
    }

    i += recordLength;
  }

  CLog::Log(LOGDEBUG, "{} - no {} in the root of \"{}\" ({} bytes of directory at sector {})",
            __FUNCTION__, folder.c_str(), image.c_str(), length, extent);
  return false;
}

/*!
 * \brief Whether a disc image holds a folder of the given name at its root
 *
 * Asked by listing the image the same way Kodi's own file browser does, which handles both
 * file systems an image may carry and costs one directory read. When that comes back empty
 * the image is read directly instead -- see Iso9660HoldsFolder for why an image Kodi cannot
 * browse is still worth identifying.
 */
bool ImageHoldsFolder(const std::string& image, const std::string& folder)
{
  CFileItemList entries;
  const bool listed = XFILE::CDirectory::GetDirectory(image, entries, "", XFILE::DIR_FLAG_DEFAULTS);

  bool found = false;
  for (int i = 0; i < entries.Size() && !found; ++i)
    found = StringUtils::EqualsNoCase(entries[i]->GetLabel(), folder);

  if (listed)
    return found;

  // Kodi could not browse the image, which is not the same as the image being unreadable
  return Iso9660HoldsFolder(image, folder);
}

//! \brief Whether any element of a path is the named folder
bool PathContainsFolder(const std::string& path, const std::string& folder)
{
  std::string rest = path;
  URIUtils::RemoveSlashAtEnd(rest);
  while (!rest.empty())
  {
    if (StringUtils::EqualsNoCase(URIUtils::GetFileName(rest), folder))
      return true;

    const std::string parent = URIUtils::GetDirectory(rest);
    if (parent.size() >= rest.size())
      break;
    rest = parent;
    URIUtils::RemoveSlashAtEnd(rest);
  }
  return false;
}
} // unnamed namespace

CDSFile::DiscType CDSFile::DiscTypeOf(const std::string& strFileName)
{
  {
    std::unique_lock<CCriticalSection> lock(g_discTypeLock);
    if (g_discTypePath == strFileName)
      return g_discTypeAnswer;
  }

  DiscType found = DiscType::None;

  // A disc folder carries its own answer in the path, and looking inside an unpacked disc
  // costs a directory read that the name has already made unnecessary
  if (PathContainsFolder(strFileName, "BDMV"))
    found = DiscType::Bluray;
  else if (PathContainsFolder(strFileName, "VIDEO_TS"))
    found = DiscType::Dvd;
  else
  {
    std::string extension = URIUtils::GetExtension(strFileName);
    StringUtils::ToLower(extension);

    // Only an image is worth opening. Everything else that reaches here is a plain media
    // file, and mounting it as a disc to find that out would cost a read per rule matched.
    if (extension == ".iso" || extension == ".img" || extension == ".udf")
    {
      // Blu-ray first, for the same reason Kodi's own CDVDFactoryInputStream does: a disc is
      // one or the other, and only a Blu-ray carries BDMV
      if (ImageHoldsFolder(strFileName, "BDMV"))
        found = DiscType::Bluray;
      else if (ImageHoldsFolder(strFileName, "VIDEO_TS"))
        found = DiscType::Dvd;

      CLog::Log(LOGDEBUG, "{} - \"{}\" holds {}", __FUNCTION__, strFileName.c_str(),
                found == DiscType::Bluray ? "a Blu-ray"
                : found == DiscType::Dvd  ? "a DVD"
                                          : "neither a Blu-ray nor a DVD");
    }
  }

  {
    std::unique_lock<CCriticalSection> lock(g_discTypeLock);
    g_discTypePath = strFileName;
    g_discTypeAnswer = found;
  }
  return found;
}

std::string CDSFile::SmbToUncPath(const std::string& strFileName)
{
  if (!StringUtils::StartsWithNoCase(strFileName, "smb://"))
    return strFileName;

  std::string strWinFileName;
  // Find first "/" after " smb://"
  int iEndOfHostNameInd = strFileName.find_first_of('/', 6);
  std::size_t found = strFileName.find_last_of('@', iEndOfHostNameInd);
  
  if (found != std::string::npos)
  {
    strWinFileName = "\\\\" + strFileName.substr(found + 1);
  }
  else
  {
    strWinFileName = strFileName;
    StringUtils::Replace(strWinFileName, "smb://", "\\\\");
  }

  StringUtils::Replace(strWinFileName, '/', '\\');

  return strWinFileName;
}

std::string CDSFile::BlurayDiscRoot(const std::string& strFileName)
{
  std::string root = strFileName;
  URIUtils::RemoveSlashAtEnd(root);

  // The disc's own root may already be what was handed over, either as the folder BDMV sits
  // in or as BDMV itself
  if (!StringUtils::EqualsNoCase(URIUtils::GetFileName(root), "BDMV"))
  {
    // Otherwise a file inside the disc structure was chosen. index.bdmv is what the file list
    // offers, and a chosen playlist sits one level deeper again.
    root = URIUtils::GetDirectory(root);
    URIUtils::RemoveSlashAtEnd(root);

    if (StringUtils::EqualsNoCase(URIUtils::GetFileName(root), "PLAYLIST"))
    {
      root = URIUtils::GetDirectory(root);
      URIUtils::RemoveSlashAtEnd(root);
    }

    // Not part of a disc folder at all: a disc image, or a plain media file
    if (!StringUtils::EqualsNoCase(URIUtils::GetFileName(root), "BDMV"))
      return strFileName;
  }

  root = URIUtils::GetDirectory(root);
  URIUtils::RemoveSlashAtEnd(root);

  return root.empty() ? strFileName : root;
}

bool CDSFile::Exists(const std::string& strFileName, long* errCode)
{
  std::string strWinFile = SmbToUncPath(strFileName);
  std::wstring strFileW;
  g_charsetConverter.utf8ToW(strWinFile, strFileW, false);

  DWORD dwAttr = GetFileAttributesW(strFileW.c_str());
  if(dwAttr != 0xffffffff)
    return true;

  if (errCode)
    *errCode = GetLastError();

  return false;
}

int CDSCharsetConverter::getCharsetIdByName(const std::string& charsetName)
{
  for (SDSCharsetMapping *c = g_charsets; c->charset; c++)
  {
    if (StringUtils::EqualsNoCase(charsetName, c->charset))
      return c->win_id;
  }

  return 1;
}

int64_t CDSTimeUtils::GetPerfCounter()
{
  LARGE_INTEGER i64Ticks100ns;
  LARGE_INTEGER llPerfFrequency;

  QueryPerformanceFrequency(&llPerfFrequency);
  if (llPerfFrequency.QuadPart != 0)
  {
    QueryPerformanceCounter(&i64Ticks100ns);
    return llMulDiv(i64Ticks100ns.QuadPart, 10000000, llPerfFrequency.QuadPart, 0);
  }
  else
  {
    // ms to 100ns units
    return timeGetTime() * 10000;
  }
}

bool CDSXMLUtils::GetInt(TiXmlElement *pElement, const std::string &attr, int *iValue)
{
  const char *str = pElement->Attribute(attr.c_str());
  if (str == NULL)
  {
    *iValue = 0;
    return false;
  }

  *iValue = atoi(str);

  return true;
}

bool CDSXMLUtils::GetFloat(TiXmlElement *pElement, const std::string &attr, float *fValue)
{
  const char *str = pElement->Attribute(attr.c_str());
  if (str == NULL)
  {
    *fValue = 0.0f;
    return false;
  }

  *fValue = (float)atof(str);

  return true;
}

bool CDSXMLUtils::GetString(TiXmlElement *pElement, const std::string &attr, std::string *sValue)
{
  const char *str = pElement->Attribute(attr.c_str());
  if (str == NULL)
  {
    *sValue = "";
    return false;
  }

  *sValue = std::string(str);

  return true;
}

std::string CDSXMLUtils::GetString(TiXmlElement *pElement, const std::string &attr)
{
  std::string s;
  GetString(pElement, attr, &s);
  return s;
}

bool CDSXMLUtils::GetTristate(TiXmlElement *pElement, const std::string &attr, int *iValue)
{
  *iValue = -1;
  const char *str = pElement->Attribute(attr.c_str()); 
  if (str == NULL)
    return false;

  if (stricmp(str, "true") == 0) *iValue = 1;
  if (stricmp(str, "false") == 0) *iValue = 0;

  return true;
}

#endif
