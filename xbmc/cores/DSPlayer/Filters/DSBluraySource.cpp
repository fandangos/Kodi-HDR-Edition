/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#if HAS_DS_PLAYER

#include <streams.h>

#include "DSBluraySource.h"

#include "threads/SingleLock.h"
#include "utils/CharsetConverter.h"
#include "utils/log.h"

#if defined(HAVE_LIBBLURAY)
#include <libbluray/bluray.h>
#endif

#include <algorithm>

namespace
{
//! Blu-ray aligned unit size. bd_seek() lands on a multiple of this, so a read that
//! starts mid unit has to skip the remainder.
constexpr DWORD BLURAY_UNIT_SIZE = 6144;
} // unnamed namespace

CDSBlurayStream::~CDSBlurayStream()
{
  Close();
}

void CDSBlurayStream::ReportThroughput(DWORD bytesRead, double readMilliseconds)
{
  m_statsBytes += bytesRead;
  m_statsReads++;
  m_statsSlowestRead = std::max(m_statsSlowestRead, readMilliseconds);

  const auto now = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double>(now - m_statsSince).count();
  if (elapsed < 10.0)
    return;

  const double megabytes = static_cast<double>(m_statsBytes) / (1024.0 * 1024.0);
  CLog::Log(LOGINFO,
            "{} - read {:.1f} MB in {:.1f}s ({:.1f} MB/s, {:.0f} Mbit/s) over {} reads, "
            "{} seek(s), slowest read {:.0f} ms",
            __FUNCTION__, megabytes, elapsed, megabytes / elapsed,
            (megabytes * 8.0) / elapsed, m_statsReads, m_statsSeeks, m_statsSlowestRead);

  m_statsSince = now;
  m_statsBytes = 0;
  m_statsReads = 0;
  m_statsSeeks = 0;
  m_statsSlowestRead = 0.0;
}

void CDSBlurayStream::Close()
{
#if defined(HAVE_LIBBLURAY)
  if (m_bd)
  {
    bd_close(m_bd);
    m_bd = nullptr;
  }
#endif
  m_length = 0;
  m_position = 0;
  m_bdPosition = 0;
}

HRESULT CDSBlurayStream::Open(const std::string& path)
{
#if !defined(HAVE_LIBBLURAY)
  CLog::Log(LOGERROR, "{} - built without libbluray", __FUNCTION__);
  return E_FAIL;
#else
  CSingleExit lock(m_lock);

  Close();
  m_path = path;

  m_bd = bd_open(path.c_str(), nullptr);
  if (!m_bd)
  {
    CLog::Log(LOGERROR, "{} - libbluray could not open \"{}\"", __FUNCTION__, path.c_str());
    return E_FAIL;
  }

  // The title list has to be read before libbluray can nominate a main title
  const uint32_t titleCount = bd_get_titles(m_bd, TITLES_RELEVANT, 0);
  if (titleCount == 0)
  {
    CLog::Log(LOGERROR, "{} - no playable titles on \"{}\"", __FUNCTION__, path.c_str());
    Close();
    return E_FAIL;
  }

  // Until title selection is wired to the GUI, play what libbluray considers the main
  // title, falling back to the longest one when it cannot decide
  int title = bd_get_main_title(m_bd);
  if (title < 0)
  {
    uint64_t longest = 0;
    for (uint32_t i = 0; i < titleCount; ++i)
    {
      BLURAY_TITLE_INFO* info = bd_get_title_info(m_bd, i, 0);
      if (!info)
        continue;

      if (info->duration > longest)
      {
        longest = info->duration;
        title = static_cast<int>(i);
      }
      bd_free_title_info(info);
    }
  }

  if (title < 0 || bd_select_title(m_bd, static_cast<uint32_t>(title)) <= 0)
  {
    CLog::Log(LOGERROR, "{} - could not select a title on \"{}\"", __FUNCTION__, path.c_str());
    Close();
    return E_FAIL;
  }

  m_length = static_cast<LONGLONG>(bd_get_title_size(m_bd));
  m_position = 0;
  m_bdPosition = 0;

  m_statsSince = std::chrono::steady_clock::now();
  m_statsBytes = 0;
  m_statsReads = 0;
  m_statsSeeks = 0;
  m_statsSlowestRead = 0.0;

  CLog::Log(LOGINFO, "{} - playing title {} of {}, {} bytes", __FUNCTION__, title, titleCount,
            m_length);
  return (m_length > 0) ? S_OK : E_FAIL;
#endif
}

HRESULT CDSBlurayStream::SetPointer(LONGLONG llPos)
{
  if (llPos < 0 || llPos > m_length)
    return S_FALSE;

  // Applied in Read(), so that a seek landing inside an aligned unit can skip forward
  // without the caller paying for it twice
  m_position = llPos;
  return S_OK;
}

HRESULT CDSBlurayStream::Read(PBYTE pbBuffer,
                              DWORD dwBytesToRead,
                              BOOL bAlign,
                              LPDWORD pdwBytesRead)
{
#if !defined(HAVE_LIBBLURAY)
  return E_FAIL;
#else
  CSingleExit lock(m_lock);

  if (pdwBytesRead)
    *pdwBytesRead = 0;

  if (!m_bd)
    return E_FAIL;

  const auto readStarted = std::chrono::steady_clock::now();

  // Reads from the splitter are almost entirely sequential, so only seek when the
  // stream is not already sitting at the requested position
  if (m_position != m_bdPosition)
  {
    m_statsSeeks++;

    // bd_seek() rounds down to an aligned unit, so read and discard the remainder to
    // land exactly where the caller asked
    const int64_t sought = bd_seek(m_bd, static_cast<uint64_t>(m_position));
    if (sought < 0)
      return E_FAIL;

    uint64_t actual = static_cast<uint64_t>(sought);
    uint8_t discard[BLURAY_UNIT_SIZE];
    while (actual < static_cast<uint64_t>(m_position))
    {
      const uint64_t remaining = static_cast<uint64_t>(m_position) - actual;
      const int want = static_cast<int>(std::min<uint64_t>(sizeof(discard), remaining));
      const int got = bd_read(m_bd, discard, want);
      if (got <= 0)
        return S_FALSE;

      actual += static_cast<uint64_t>(got);
    }

    m_bdPosition = static_cast<LONGLONG>(actual);
  }

  DWORD done = 0;
  while (done < dwBytesToRead)
  {
    const int got = bd_read(m_bd, pbBuffer + done, static_cast<int>(dwBytesToRead - done));
    if (got <= 0)
      break;

    done += static_cast<DWORD>(got);
  }

  m_position += done;
  m_bdPosition = m_position;
  if (pdwBytesRead)
    *pdwBytesRead = done;

  const double readMilliseconds =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readStarted)
          .count();
  ReportThroughput(done, readMilliseconds);

  // A short read at the end of the title is not an error
  return S_OK;
#endif
}

LONGLONG CDSBlurayStream::Size(LONGLONG* pSizeAvailable)
{
  if (pSizeAvailable)
    *pSizeAvailable = m_length;

  return m_length;
}

DWORD CDSBlurayStream::Alignment()
{
  return 1;
}

void CDSBlurayStream::Lock()
{
  m_lock.lock();
}

void CDSBlurayStream::Unlock()
{
  m_lock.unlock();
}

CDSBlurayReader::CDSBlurayReader(LPUNKNOWN pUnknown, HRESULT* phr)
  : CAsyncReader(NAME("Kodi Blu-ray Reader\0"), pUnknown, &m_stream, phr), m_stream()
{
  m_mt.majortype = MEDIATYPE_Stream;
  m_mt.subtype = MEDIASUBTYPE_MPEG2_TRANSPORT;
}

HRESULT STDMETHODCALLTYPE CDSBlurayReader::Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt)
{
  std::string path;
  g_charsetConverter.wToUTF8(pszFileName, path);

  CLog::Log(LOGINFO, "{} - opening \"{}\"", __FUNCTION__, path.c_str());
  return m_stream.Open(path);
}

STDMETHODIMP CDSBlurayReader::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
  CheckPointer(ppv, E_POINTER);

  return (riid == __uuidof(IFileSourceFilter)) ? GetInterface((IFileSourceFilter*)this, ppv)
                                               : __super::NonDelegatingQueryInterface(riid, ppv);
}

#endif
