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

#include "ServiceBroker.h"
#include "cores/DSPlayer/DSBlurayNavigator.h"
#include "cores/DSPlayer/Utils/DSFileUtils.h"
#include "utils/StringUtils.h"
#include "settings/DiscSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "threads/SingleLock.h"
#include "utils/CharsetConverter.h"
#include "utils/log.h"
#include "utils/XTimeUtils.h"

using namespace std::chrono_literals;

#if defined(HAVE_LIBBLURAY)
#include <libbluray/bluray.h>
#endif

#include <algorithm>

namespace
{
//! Blu-ray aligned unit size. bd_seek() lands on a multiple of this, so a read that
//! starts mid unit has to skip the remainder.
constexpr DWORD BLURAY_UNIT_SIZE = 6144;

//! How far back a navigated stream can be read. The splitter reads forwards and only
//! looks back over the packets it is currently parsing, so this needs to cover a moment
//! of parsing rather than any real seek.
constexpr size_t HISTORY_SIZE = 16 * 1024 * 1024;

//! Bytes pulled from the disc in one go when the history window has to grow
constexpr int PULL_SIZE = 128 * 1024;

//! How far a navigated stream will skip forwards by throwing bytes away. A splitter
//! probing for the end of the file asks for a position far beyond anything the disc has
//! played, and reading the whole way there would burn through the disc.
constexpr LONGLONG MAX_FORWARD_SKIP = 8 * 1024 * 1024;

//! Reported as the distance still to come in navigation mode, where the real length is
//! not known until the disc has finished deciding what to play. It only has to be large
//! enough that the splitter treats the stream as unfinished rather than truncated.
constexpr LONGLONG NAVIGATION_LOOKAHEAD = 256 * 1024 * 1024;
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
  m_navigator.reset();
  m_history.clear();
  m_history.shrink_to_fit();
  m_historyBytes = 0;
  m_historyEnd = 0;
  m_produced = 0;
  m_exhausted = false;

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
  std::unique_lock<CCriticalSection> lock(m_lock);

  Close();
  m_path = path;

  m_statsSince = std::chrono::steady_clock::now();
  m_statsBytes = 0;
  m_statsReads = 0;
  m_statsSeeks = 0;
  m_statsSlowestRead = 0.0;

  // Kodi already lets the user say they only ever want the main title. Anything else means
  // the disc should be allowed to present itself.
  const int mode =
      CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DISC_PLAYBACK);

  if (mode != BD_PLAYBACK_MAIN_TITLE && OpenNavigation(path))
    return S_OK;

  return OpenTitle(path);
#endif
}

bool CDSBlurayStream::OpenNavigation(const std::string& path)
{
  auto navigator = std::make_unique<CDSBlurayNavigator>();
  if (!navigator->Open(path))
    return false;

  m_history.assign(HISTORY_SIZE, 0);
  m_historyBytes = 0;
  m_historyEnd = 0;
  m_produced = 0;
  m_exhausted = false;
  m_navigator = std::move(navigator);

  // The splitter inspects the opening bytes before it will connect, so the disc has to have
  // started playing something before the graph is built. Give it a moment: a disc spends
  // the first instant loading its menu code, and BD-J discs start a Java VM to do it.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (m_produced == 0 && !m_exhausted && std::chrono::steady_clock::now() < deadline)
  {
    if (Pull() == 0)
      KODI::TIME::Sleep(50ms);
  }

  if (m_produced == 0)
  {
    CLog::Log(LOGERROR, "{} - \"{}\" navigated but produced nothing to play", __FUNCTION__, path);
    m_navigator.reset();
    return false;
  }

  CLog::Log(LOGINFO, "{} - the disc started playing after {} bytes", __FUNCTION__, m_produced);

  CLog::Log(LOGINFO, "{} - reading \"{}\" through its own menus", __FUNCTION__, path);
  return true;
}

HRESULT CDSBlurayStream::OpenTitle(const std::string& kodiPath)
{
#if !defined(HAVE_LIBBLURAY)
  return E_FAIL;
#else
  // Reading a title directly means libbluray opens the file itself, and it can only do that
  // with a path Windows understands
  std::string path = CDSFile::SmbToUncPath(kodiPath);
  StringUtils::Replace(path, "/", "\\");

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

  // Play what libbluray considers the main title, falling back to the longest one when it
  // cannot decide
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

  CLog::Log(LOGINFO, "{} - playing title {} of {} directly, {} bytes", __FUNCTION__, title,
            titleCount, m_length);
  return (m_length > 0) ? S_OK : E_FAIL;
#endif
}

HRESULT CDSBlurayStream::SetPointer(LONGLONG llPos)
{
  if (llPos < 0)
    return S_FALSE;

  if (!m_navigator && llPos > m_length)
    return S_FALSE;

  // Applied in Read(), so that a seek landing inside an aligned unit can skip forward
  // without the caller paying for it twice
  m_position = llPos;
  return S_OK;
}

DWORD CDSBlurayStream::Pull()
{
  if (m_exhausted)
    return 0;

  uint8_t buffer[PULL_SIZE];
  const int read = m_navigator->Read(buffer, sizeof(buffer));
  if (read < 0)
  {
    CLog::Log(LOGERROR, "{} - the disc reported a read error", __FUNCTION__);
    m_exhausted = true;
    return 0;
  }

  if (read == 0)
  {
    // Nothing to read is the normal state of a menu waiting for the viewer, so it only
    // means the end of the stream when the disc says it has finished
    if (m_navigator->Finished())
    {
      CLog::Log(LOGINFO, "{} - the disc has finished after {} bytes", __FUNCTION__, m_produced);
      m_exhausted = true;
    }
    return 0;
  }

  // Append to the history window, oldest bytes falling out of the far end
  size_t offset = 0;
  size_t remaining = static_cast<size_t>(read);
  if (remaining > m_history.size())
  {
    // Cannot happen while PULL_SIZE is below the window size, but dropping the bytes that
    // would be overwritten anyway keeps the arithmetic below true
    offset = remaining - m_history.size();
    remaining = m_history.size();
  }

  while (remaining > 0)
  {
    const size_t chunk = std::min(remaining, m_history.size() - m_historyEnd);
    std::copy_n(buffer + offset, chunk, m_history.begin() + m_historyEnd);

    m_historyEnd = (m_historyEnd + chunk) % m_history.size();
    offset += chunk;
    remaining -= chunk;
  }

  m_historyBytes = std::min(m_history.size(), m_historyBytes + static_cast<size_t>(read));
  m_produced += read;
  return static_cast<DWORD>(read);
}

void CDSBlurayStream::CopyFromHistory(LONGLONG position, PBYTE buffer, DWORD length) const
{
  // How far back in the window the read starts, counted from the newest byte
  const size_t back = static_cast<size_t>(m_produced - position);
  size_t index = (m_historyEnd + m_history.size() - back) % m_history.size();

  DWORD copied = 0;
  while (copied < length)
  {
    const size_t chunk = std::min<size_t>(length - copied, m_history.size() - index);
    std::copy_n(m_history.begin() + index, chunk, buffer + copied);

    index = (index + chunk) % m_history.size();
    copied += static_cast<DWORD>(chunk);
  }
}

HRESULT CDSBlurayStream::ReadNavigation(PBYTE pbBuffer, DWORD dwBytesToRead, LPDWORD pdwBytesRead)
{
  if (dwBytesToRead > m_history.size() / 2)
  {
    CLog::Log(LOGERROR, "{} - asked for {} bytes at once, more than the window holds",
              __FUNCTION__, dwBytesToRead);
    return E_FAIL;
  }

  // A navigated disc cannot be rewound, so a read that starts before the window is one we
  // will never be able to answer
  if (m_position < m_produced - static_cast<LONGLONG>(m_historyBytes))
  {
    CLog::Log(LOGWARNING, "{} - cannot go back to {}, the disc is already at {}", __FUNCTION__,
              m_position, m_produced);
    return S_FALSE;
  }

  if (m_position - m_produced > MAX_FORWARD_SKIP)
  {
    // Almost certainly the splitter looking for the end of the file, which a disc that is
    // still deciding what to play does not have
    CLog::Log(LOGDEBUG, "{} - refusing to skip forward from {} to {}", __FUNCTION__, m_produced,
              m_position);
    return S_FALSE;
  }

  // Reaching a position the disc has not played yet means reading and discarding until it
  // gets there
  while (m_produced < m_position)
  {
    if (Pull() == 0)
      return S_FALSE;
  }

  while (m_produced < m_position + dwBytesToRead)
  {
    if (Pull() == 0)
      break;
  }

  const DWORD available =
      static_cast<DWORD>(std::min<LONGLONG>(dwBytesToRead, m_produced - m_position));
  if (available > 0)
  {
    CopyFromHistory(m_position, pbBuffer, available);
    m_position += available;
  }

  if (pdwBytesRead)
    *pdwBytesRead = available;

  return (available == dwBytesToRead) ? S_OK : S_FALSE;
}

HRESULT CDSBlurayStream::ReadTitle(PBYTE pbBuffer, DWORD dwBytesToRead, LPDWORD pdwBytesRead)
{
#if !defined(HAVE_LIBBLURAY)
  return E_FAIL;
#else
  // Reads from the splitter are almost entirely sequential, so only seek when the stream is
  // not already sitting at the requested position
  if (m_position != m_bdPosition)
  {
    m_statsSeeks++;

    // bd_seek() rounds down to an aligned unit, so read and discard the remainder to land
    // exactly where the caller asked
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

  // A short read at the end of the title is not an error
  return S_OK;
#endif
}

HRESULT CDSBlurayStream::Read(PBYTE pbBuffer,
                              DWORD dwBytesToRead,
                              BOOL bAlign,
                              LPDWORD pdwBytesRead)
{
  // The reader already holds this through Lock(), but Size() is called from elsewhere and
  // the section is recursive
  std::unique_lock<CCriticalSection> lock(m_lock);

  if (pdwBytesRead)
    *pdwBytesRead = 0;

  if (!m_navigator && !m_bd)
    return E_FAIL;

  const auto readStarted = std::chrono::steady_clock::now();

  DWORD done = 0;
  const HRESULT hr = m_navigator ? ReadNavigation(pbBuffer, dwBytesToRead, &done)
                                 : ReadTitle(pbBuffer, dwBytesToRead, &done);

  if (pdwBytesRead)
    *pdwBytesRead = done;

  const double readMilliseconds =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readStarted)
          .count();
  ReportThroughput(done, readMilliseconds);

  return hr;
}

LONGLONG CDSBlurayStream::Size(LONGLONG* pSizeAvailable)
{
  // Asked from whichever thread wants to know how long the stream is, while the streaming
  // thread is still adding to it
  std::unique_lock<CCriticalSection> lock(m_lock);

  if (m_navigator)
  {
    // Only what the disc has played so far exists. Describing the stream as unfinished
    // rather than as a file of that length stops the splitter treating the end of what we
    // have as the end of the disc.
    if (pSizeAvailable)
      *pSizeAvailable = m_produced;

    return m_exhausted ? m_produced : m_produced + NAVIGATION_LOOKAHEAD;
  }

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
