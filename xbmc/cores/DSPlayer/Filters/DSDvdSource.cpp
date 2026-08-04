/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#if HAS_DS_PLAYER

#include <streams.h>

#include "DSDvdSource.h"

#include "cores/DSPlayer/DSDvdNavigator.h"
#include "cores/DSPlayer/DSPlayer.h"
#include "utils/CharsetConverter.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"

using namespace std::chrono_literals;

#include <algorithm>
#include <thread>

namespace
{
//! A DVD is made of 2048 byte blocks and libdvdnav deals in whole ones, refusing a buffer
//! smaller than a single block
constexpr DWORD DVD_BLOCK_SIZE = 2048;

//! How far back a navigated stream can be read. Bigger than a moment of parsing needs,
//! because the splitter hunts for the end of the stream by reading near it and backing off
//! in doubling steps.
constexpr size_t HISTORY_SIZE = 32 * 1024 * 1024;

//! Bytes pulled from the disc in one go. A whole number of DVD blocks, necessarily.
constexpr int PULL_SIZE = 64 * DVD_BLOCK_SIZE;

//! How far a navigated stream will skip forwards by throwing bytes away. A splitter probing
//! for the end of the file asks for a position far beyond anything the disc has played.
constexpr LONGLONG MAX_FORWARD_SKIP = 8 * 1024 * 1024;

//! Reported as the distance still to come, where the real length is not known until the disc
//! has finished deciding what to play. Deliberately small, and below MAX_FORWARD_SKIP so that
//! reaching the claimed end is allowed rather than refused -- see the Blu-ray source for what
//! claiming a length the disc cannot reach does to a splitter.
constexpr LONGLONG NAVIGATION_LOOKAHEAD = 2 * 1024 * 1024;

//! How long a disc gets to start playing something before navigation is abandoned. A DVD has
//! no Java VM to start, so this is far shorter than the Blu-ray equivalent.
constexpr std::chrono::milliseconds NAVIGATION_START_TIMEOUT{15000};

//! Longest to spend waiting for the disc to stop changing title before taking what it is on
constexpr std::chrono::milliseconds NAVIGATION_SETTLE_LIMIT{8000};

//! How long one read will wait for the disc before saying so in the log
constexpr std::chrono::milliseconds NAVIGATION_READ_TIMEOUT{1000};

//! How many of the opening reads to trace
constexpr uint32_t TRACED_READS = 60;

//! How long the disc has to stay on one title before its bytes are worth showing the
//! splitter.
//!
//! This has to outlast the programmes a disc passes through on its way in, not just the gaps
//! between them. You're Not Elected opens on title 3, a copyright notice, and sat on it for
//! two and a half seconds of reading before moving to 5 and then to 18 -- so settling at
//! 2500 ms handed the splitter a programme the disc had already left, and the disc then
//! changed twice more while the splitter was still scanning it.
constexpr std::chrono::milliseconds NAVIGATION_SETTLE{6000};

//! How long the disc has to stay put once it says it is showing a menu. Short, because
//! reaching a menu is the disc telling us it has arrived: it is where a first play sequence
//! was heading, and it waits there for the viewer.
constexpr std::chrono::milliseconds MENU_SETTLE{700};

//! How often to say where the disc has got to while it is settling. Settling is guesswork
//! about a state machine we cannot see into, and every fault in it so far has been read off
//! this trace rather than reasoned about.
constexpr std::chrono::milliseconds SETTLE_TRACE{500};

//! Most that is kept while waiting for the disc to settle. Only there to stop a disc that
//! reads faster than it plays from running away with a whole title.
constexpr LONGLONG SETTLE_CAP = 4 * 1024 * 1024;

//! How much of the settled programme to have ready before the graph is built. Well inside
//! HISTORY_SIZE, so the splitter can still reach the first byte while it scans.
constexpr LONGLONG PRIME_BYTES = 1024 * 1024;

//! Longest to spend collecting those opening bytes before going with what arrived
constexpr std::chrono::milliseconds PRIME_TIME{4000};

//! How far ahead of the splitter the disc is read
constexpr LONGLONG READAHEAD = 4 * 1024 * 1024;

//! How far the disc is allowed to run before the graph is built. Comfortably inside
//! HISTORY_SIZE, so everything read while the splitter works out what the stream holds is
//! still there when it goes back to the beginning to play it.
constexpr LONGLONG OPENING_BYTES_HELD = 24 * 1024 * 1024;

//! How long the readahead cap is ignored after the viewer presses something, see
//! LetTheDiscRunOn. Long enough for a disc to get from a menu to whatever the button chose,
//! short enough that a button leading nowhere is not noticed.
constexpr auto RUN_ON_AFTER_INPUT = std::chrono::seconds(4);

/*!
 * \brief Whether a DVD block carries the start of an MPEG-2 video sequence
 *
 * The splitter has to find a picture in the bytes it is given or it exposes no video pin at
 * all, and a DVD menu is often a single still frame with a minute of audio over it: one
 * I-frame at the head of the cell and nothing after it. Handing over a stream that begins
 * after that frame gives LAV Splitter audio and subpicture and no video, which looks exactly
 * like working playback -- the graph runs and the position advances -- except that madVR is
 * never configured and the screen stays black.
 *
 * Scanning for the sequence header start code is enough. It cannot straddle a block, because
 * a DVD pack is a whole 2048 bytes and a video PES starts inside one.
 */
bool HasSequenceHeader(const uint8_t* block, size_t length)
{
  if (length < 4)
    return false;

  for (size_t i = 0; i + 3 < length; ++i)
  {
    if (block[i] == 0x00 && block[i + 1] == 0x00 && block[i + 2] == 0x01 && block[i + 3] == 0xb3)
      return true;
  }
  return false;
}
} // unnamed namespace

CDSDvdStream* CDSDvdStream::m_feeding = nullptr;

void CDSDvdStream::StopFeedingTheGraph()
{
  CDSDvdStream* stream = m_feeding;
  if (!stream)
    return;

  CLog::Log(LOGDEBUG, "{} - no more bytes for the graph, it is being taken down", __FUNCTION__);
  stream->m_producerStop = true;
  stream->m_ringGrew.notify_all();
}

void CDSDvdStream::FollowTheDisc()
{
  CDSDvdStream* stream = m_feeding;
  if (!stream)
    return;

  CLog::Log(LOGINFO, "{} - the programme has ended, following the disc to the next one",
            __FUNCTION__);
  stream->m_followDisc = true;
}

void CDSDvdStream::LetTheDiscRunOn()
{
  CDSDvdStream* stream = m_feeding;
  if (!stream)
    return;

  const auto until = std::chrono::steady_clock::now() + RUN_ON_AFTER_INPUT;
  stream->m_runOnUntil = until.time_since_epoch().count();
}

void CDSDvdStream::NoteGraphBuilt()
{
  CDSDvdStream* stream = m_feeding;
  if (!stream || !stream->m_holdingOpeningBytes)
    return;

  CLog::Log(LOGINFO, "{} - the graph is built, the disc may run on from its opening bytes",
            __FUNCTION__);
  stream->m_holdingOpeningBytes = false;

  // And only now is a change of programme worth reporting. Until this point the splitter was
  // scanning, and scanning reads the disc, and reading it moves it on -- so every title it
  // passed through while being parsed would have asked for the graph to be built again on
  // something the splitter had not finished looking at.
  if (stream->m_navigator)
    stream->m_navigator->AnnounceProgrammeChanges();
}

void CDSDvdStream::NoteGraphRunning()
{
  CDSDvdStream* stream = m_feeding;
  if (!stream || !stream->m_holdingOpeningBytes)
    return;

  CLog::Log(LOGINFO, "{} - playback has started, the disc may run on from its opening bytes",
            __FUNCTION__);
  stream->m_holdingOpeningBytes = false;
}

CDSDvdStream::~CDSDvdStream()
{
  Close();
}

void CDSDvdStream::ReportThroughput(DWORD bytesRead, double readMilliseconds)
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
            "slowest read {:.0f} ms",
            __FUNCTION__, megabytes, elapsed, megabytes / elapsed, (megabytes * 8.0) / elapsed,
            m_statsReads, m_statsSlowestRead);

  m_statsSince = now;
  m_statsBytes = 0;
  m_statsReads = 0;
  m_statsSlowestRead = 0.0;
}

void CDSDvdStream::Close()
{
  // Stop reading, but leave the disc alone: this also runs when the graph is being rebuilt
  // because the disc moved to another programme, and giving up on the disc there would end
  // the very thing being followed. Reads are bounded, so the thread comes back on its own.
  if (m_feeding == this)
    m_feeding = nullptr;

  m_producerStop = true;
  m_ringGrew.notify_all();
  if (m_producer.joinable())
    m_producer.join();

  m_navigator.reset();
  m_history.clear();
  m_history.shrink_to_fit();
  m_historyBytes = 0;
  m_historyEnd = 0;
  m_produced = 0;
  m_exhausted = false;
  m_position = 0;
  m_title = 0;

  // Open() closes before it opens, so these have to come back to what they mean for a stream
  // that has not started yet, or the next disc inherits the last one's graph
  m_holdingOpeningBytes = true;
  m_followDisc = false;
}

HRESULT CDSDvdStream::Open(const std::string& path)
{
  Close();

  m_path = path;

  if (!OpenNavigation(path))
  {
    CLog::Log(LOGERROR, "{} - could not play \"{}\"", __FUNCTION__, path.c_str());
    return E_FAIL;
  }

  return S_OK;
}

bool CDSDvdStream::OpenNavigation(const std::string& path)
{
  // The disc may already be playing, if this graph is being built again because the disc
  // moved from its menu to a title. In that case carry on from where it is rather than
  // starting the disc over.
  const bool resuming = (CDSDvdNavigator::Get() != nullptr);

  std::shared_ptr<CDSDvdNavigator> navigator = CDSDvdNavigator::Session(path);
  if (!navigator)
    return false;

  m_history.assign(HISTORY_SIZE, 0);
  m_historyBytes = 0;
  m_historyEnd = 0;
  m_produced = 0;
  m_exhausted = false;
  m_navigator = std::move(navigator);

  // The splitter inspects the opening bytes before it will connect, so the disc has to have
  // started playing something before the graph is built.
  //
  // The waiting cannot be done by watching the clock here, because a disc that never produces
  // anything never returns from the read at all. It takes another thread to call time on it.
  std::atomic<bool> started{false};
  std::thread watchdog(
      [this, &started]()
      {
        for (int waited = 0; waited < NAVIGATION_START_TIMEOUT.count() && !started; waited += 100)
          KODI::TIME::Sleep(100ms);

        if (!started)
        {
          CLog::Log(LOGWARNING, "{} - the disc has played nothing in {}s, abandoning navigation",
                    __FUNCTION__, NAVIGATION_START_TIMEOUT.count() / 1000);
          m_navigator->Abort();
        }
      });

  // A DVD runs through several programmes on its way in: a copyright notice, a distributor
  // logo, then whatever it settles on, usually its menu. Each is a separate programme, and
  // the splitter is given one stream and reads it as one file, so the bytes it is shown have
  // to belong to a single one of them. Play until the disc stops changing its mind, throwing
  // away what belongs to the programmes it passed through.
  // Pull cuts the stream at the exact block the disc changes programme on while this is set,
  // so what is collected always belongs to one programme
  m_settling = true;
  m_collecting = m_navigator->Cell();
  m_haveVideo = false;

  int title = m_navigator->Title();
  uint32_t collecting = m_collecting;
  const auto waitingSince = std::chrono::steady_clock::now();
  auto settledAt = waitingSince;
  auto tracedAt = waitingSince;
  //! When the disc first put a menu up, which is not the same as when it last changed cell:
  //! a menu that loops changes cell every time round, and on a disc read far faster than it
  //! plays that is many times a second
  std::chrono::steady_clock::time_point inMenuSince{};

  while (!m_exhausted)
  {
    // Keep reading the whole time. A disc only advances while it is being read, so pausing
    // here stops it dead where it stands.
    if (Pull() == 0)
      KODI::TIME::Sleep(10ms);

    // Only a cap on runaway reading. The cell boundary itself is cut inside Pull, which is
    // the only place that sees the stream block by block.
    if (m_produced > SETTLE_CAP)
      Discard();

    const int nowTitle = m_navigator->Title();
    if (m_collecting != collecting || nowTitle != title)
    {
      collecting = m_collecting;
      title = nowTitle;
      settledAt = std::chrono::steady_clock::now();
    }

    const auto now = std::chrono::steady_clock::now();

    if (m_navigator->MenuOnScreen())
    {
      if (inMenuSince == std::chrono::steady_clock::time_point{})
        inMenuSince = now;
    }
    else
      inMenuSince = {};

    if (now - tracedAt >= SETTLE_TRACE)
    {
      tracedAt = now;
      CLog::Log(LOGDEBUG, "{} - settling: title {}, {} a menu, {} still, {} video, {} bytes",
                __FUNCTION__, title, m_navigator->MenuOnScreen() ? "in" : "not in",
                m_navigator->HoldingIndefiniteStill() ? "holding a" : "no",
                m_haveVideo ? "with" : "no", static_cast<LONGLONG>(m_produced));
    }

    // Nothing below may finish the settle until there is a picture in what has been
    // collected. A DVD menu is very often one I-frame at the head of its cell followed by a
    // minute of audio, and a stream handed over without that frame gives the splitter no
    // video pin at all -- the graph runs, the position advances, and the screen stays black.
    if (m_haveVideo)
    {
      // A disc holding a still is showing a menu and waiting for the viewer. Nothing more is
      // coming until they choose, so there is nothing left to settle.
      if (m_navigator->HoldingIndefiniteStill() && m_produced > 0)
      {
        CLog::Log(LOGDEBUG, "{} - the disc is holding a still on title {}, which is its menu",
                  __FUNCTION__, title);
        break;
      }

      // Waiting for the disc to go quiet is guesswork, and guessing wrong means handing the
      // splitter a programme the disc is about to leave -- which is exactly what settling on
      // a copyright notice did. The disc says where it was heading: once it is showing a
      // menu, that is what to show, because a menu is where a first play sequence ends.
      //
      // Measured from when the menu went up rather than from the last cell change. A menu
      // that loops starts a new cell every time round, and read at thirteen times playing
      // speed that reset the clock every 78 ms -- so this never fired at all and the settle
      // fell through to its eight second backstop, having read most of the menu and thrown
      // it away.
      if (inMenuSince != std::chrono::steady_clock::time_point{} &&
          now - inMenuSince >= MENU_SETTLE && m_produced > 0)
      {
        CLog::Log(LOGDEBUG, "{} - the disc reached a menu on title {}", __FUNCTION__, title);
        break;
      }

      // A disc already playing is only having its graph rebuilt: it has chosen already
      if (resuming && m_produced >= PRIME_BYTES)
        break;

      if (now - settledAt >= NAVIGATION_SETTLE)
        break;
    }

    // Some discs never stop moving, and waiting for a quiet moment that never comes is worse
    // than taking whatever is playing now
    if (now - waitingSince >= NAVIGATION_SETTLE_LIMIT)
    {
      CLog::Log(LOGDEBUG, "{} - the disc is still moving about after {}s, taking title {}",
                __FUNCTION__, NAVIGATION_SETTLE_LIMIT.count() / 1000, title);
      break;
    }

    // A disc read from a file runs far faster than it plays, and every byte read here is a
    // byte of the disc played and thrown away
    KODI::TIME::Sleep(5ms);
  }

  // Settling may have landed just after the collected bytes were thrown away, so top up
  // enough for the splitter to parse. Nothing is discarded here, and a menu with nothing more
  // to give simply runs out the clock with what it already handed over.
  const auto topUpUntil = std::chrono::steady_clock::now() + PRIME_TIME;
  while (m_produced < PRIME_BYTES && !m_exhausted &&
         std::chrono::steady_clock::now() < topUpUntil)
  {
    if (Pull() == 0)
      KODI::TIME::Sleep(10ms);
  }

  // From here the stream is what the splitter is parsing, and nothing may be thrown out of
  // the front of it
  m_settling = false;

  CLog::Log(LOGDEBUG, "{} - title {} gave {} bytes to open with", __FUNCTION__, title,
            static_cast<LONGLONG>(m_produced));

  started = true;
  watchdog.join();

  m_title = title;

  if (m_produced == 0)
  {
    CLog::Log(LOGERROR, "{} - \"{}\" navigated but produced nothing to play", __FUNCTION__, path);
    m_navigator.reset();
    return false;
  }

  // From here the disc is read on its own thread, so that a read never waits on it
  m_producerStop = false;
  m_feeding = this;
  m_producer = std::thread(&CDSDvdStream::Produce, this);

  // Deliberately not announcing programme changes yet. Between here and the graph being
  // built the splitter scans the stream, and scanning means reading, and reading is what
  // moves the disc: on the first run of this the disc went through two more titles during
  // the scan, each was reported, and the graph was rebuilt out from under itself before it
  // had ever run. NoteGraphBuilt starts the reporting instead, by which time the splitter is
  // demuxing rather than racing ahead.

  CLog::Log(LOGINFO, "{} - reading \"{}\" through its own menus", __FUNCTION__, path);
  return true;
}

HRESULT CDSDvdStream::SetPointer(LONGLONG llPos)
{
  if (llPos < 0)
    return S_FALSE;

  // Applied in Read()
  m_position = llPos;
  return S_OK;
}

void CDSDvdStream::Append(const uint8_t* bytes, size_t length)
{
  std::lock_guard<std::mutex> ring(m_ringMutex);

  // Append to the history window, oldest bytes falling out of the far end
  size_t offset = 0;
  size_t remaining = length;
  if (remaining > m_history.size())
  {
    offset = remaining - m_history.size();
    remaining = m_history.size();
  }

  while (remaining > 0)
  {
    const size_t chunk = std::min(remaining, m_history.size() - m_historyEnd);
    std::copy_n(bytes + offset, chunk, m_history.begin() + m_historyEnd);

    m_historyEnd = (m_historyEnd + chunk) % m_history.size();
    offset += chunk;
    remaining -= chunk;
  }

  m_historyBytes = std::min(m_history.size(), m_historyBytes + length);
  m_produced += length;
}

DWORD CDSDvdStream::Pull()
{
  if (m_exhausted || !m_navigator)
    return 0;

  uint8_t block[DVD_BLOCK_SIZE];
  DWORD added = 0;
  bool failed = false;

  // libdvdnav hands over exactly one 2048 byte block per call, so filling a buffer takes as
  // many calls as it holds blocks. Taking a single block per Pull and then sleeping between
  // them held the disc to 370 kB/s: six seconds of settling never got past the copyright
  // notice, so the graph was built on a fourteen second anti-piracy card while the disc's
  // menu was still two titles away.
  while (added + DVD_BLOCK_SIZE <= PULL_SIZE)
  {
    const int read = m_navigator->Read(block, sizeof(block));
    if (read < 0)
    {
      failed = true;
      break;
    }

    // Nothing more just now: a menu waiting for the viewer, or the disc between programmes.
    // Hand over what did arrive rather than holding it back.
    if (read == 0)
      break;

    // While settling, the disc is being read to find out where it is going and what it passes
    // through is thrown away. That has to be cut at the exact block the disc changes
    // programme on, and this is the only place that knows: a batch of blocks straddles the
    // boundary, and discarding the whole batch takes the new programme's opening bytes with
    // it. On this disc that meant throwing away the menu's only I-frame, after which LAV
    // Splitter found nothing but an Audio pin -- the graph ran, the position advanced, and
    // madVR was never configured because no video ever reached it.
    if (m_settling)
    {
      const uint32_t cell = m_navigator->Cell();
      if (cell != m_collecting)
      {
        CLog::Log(LOGDEBUG,
                  "{} - the disc moved to another cell (title {}) after {} bytes{}", __FUNCTION__,
                  m_navigator->Title(), static_cast<LONGLONG>(m_produced),
                  m_haveVideo ? "" : ", which carried no video");
        m_collecting = cell;
        Discard();
        added = 0;
      }
    }

    if (HasSequenceHeader(block, static_cast<size_t>(read)))
      m_haveVideo = true;

    Append(block, static_cast<size_t>(read));
    added += static_cast<DWORD>(read);
  }

  if (failed && added == 0)
  {
    CLog::Log(LOGERROR, "{} - the disc reported a read error", __FUNCTION__);
    m_exhausted = true;
    return 0;
  }

  if (added == 0)
  {
    // Nothing to read is the normal state of a menu waiting for the viewer, so it only means
    // the end of the stream when the disc says it has finished
    if (m_navigator->Finished())
    {
      CLog::Log(LOGINFO, "{} - the disc has finished after {} bytes", __FUNCTION__,
                static_cast<LONGLONG>(m_produced));
      m_exhausted = true;
    }
    return 0;
  }

  return added;
}

void CDSDvdStream::Produce()
{
  while (!m_producerStop && !m_exhausted)
  {
    // A moment of reading flat out because the viewer has just pressed something. The disc
    // acts on a button only as it is read, so everything already queued has to play out
    // first -- fifteen seconds of it, on a menu -- unless the disc is let off the leash.
    const auto runOn = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(m_runOnUntil.load()));
    const bool answeringTheViewer = std::chrono::steady_clock::now() < runOn;

    // Keep a little ahead of where the splitter is reading, no more. Running further ahead
    // would push the bytes it may still ask for out of the far end of the window. The one
    // exception is when the splitter has stopped reading because it believes its programme
    // has ended: the disc has to be kept moving or it never reaches whatever comes next.
    if (m_produced - m_position > READAHEAD && !m_followDisc && !answeringTheViewer)
    {
      KODI::TIME::Sleep(5ms);
      continue;
    }

    // While the splitter is still scanning, hold the disc near where it started. A splitter
    // reads forward through the stream to work out what is in it and then goes back to the
    // beginning to play it, and a navigated disc cannot be rewound.
    if (m_holdingOpeningBytes && !m_followDisc && m_produced >= OPENING_BYTES_HELD)
    {
      KODI::TIME::Sleep(5ms);
      continue;
    }

    if (Pull() > 0)
      m_ringGrew.notify_all();
  }

  // Whatever is waiting for bytes should stop waiting
  m_ringGrew.notify_all();
}

void CDSDvdStream::Discard()
{
  m_historyBytes = 0;
  m_historyEnd = 0;
  m_produced = 0;
  m_position = 0;
  // Whatever picture there was went with the bytes
  m_haveVideo = false;
}

void CDSDvdStream::CopyFromHistory(LONGLONG position, PBYTE buffer, DWORD length) const
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

HRESULT CDSDvdStream::Read(PBYTE pbBuffer, DWORD dwBytesToRead, BOOL bAlign, LPDWORD pdwBytesRead)
{
  // The reader already holds this through Lock(), but Size() is called from elsewhere and the
  // section is recursive
  std::unique_lock<CCriticalSection> lock(m_lock);

  if (pdwBytesRead)
    *pdwBytesRead = 0;

  if (!m_navigator)
    return E_FAIL;

  if (dwBytesToRead > m_history.size() / 2)
  {
    CLog::Log(LOGERROR, "{} - asked for {} bytes at once, more than the window holds",
              __FUNCTION__, dwBytesToRead);
    return E_FAIL;
  }

  const auto readStarted = std::chrono::steady_clock::now();
  const LONGLONG asked = m_position;

  HRESULT hr = S_OK;
  DWORD done = 0;

  // A navigated disc cannot be rewound, so a read that starts before the window is one we
  // will never be able to answer
  if (m_position < m_produced - static_cast<LONGLONG>(m_historyBytes))
  {
    CLog::Log(LOGWARNING, "{} - cannot go back to {}, the disc is already at {}", __FUNCTION__,
              m_position, static_cast<LONGLONG>(m_produced));
    hr = S_FALSE;
  }
  else if (m_position - m_produced > MAX_FORWARD_SKIP)
  {
    // Almost certainly the splitter looking for the end of the file, which a disc that is
    // still deciding what to play does not have
    CLog::Log(LOGDEBUG, "{} - refusing to skip forward from {} to {}", __FUNCTION__,
              static_cast<LONGLONG>(m_produced), m_position);
    hr = S_FALSE;
  }
  else
  {
    if (m_position > m_produced)
    {
      CLog::Log(LOGDEBUG, "{} - playing forward {} bytes to reach {}", __FUNCTION__,
                m_position - m_produced, m_position);
    }

    const LONGLONG wanted = m_position + static_cast<LONGLONG>(dwBytesToRead);

    // While the disc is being held near its start, bytes beyond what is held are never going
    // to arrive, so waiting for them would wait for ever. Saying the stream ends there is what
    // stops the splitter scanning further, which is the point of holding it -- and it is only
    // safe while it is still scanning, which is why the hold ends at NoteGraphBuilt.
    if (m_holdingOpeningBytes && wanted > OPENING_BYTES_HELD)
    {
      if (pdwBytesRead)
        *pdwBytesRead = 0;
      return S_FALSE;
    }

    // Wait for the producer to have the bytes, never for the disc itself. The wait releases
    // the ring, so nothing that only wants the length is held up by it.
    {
      std::unique_lock<std::mutex> ring(m_ringMutex);
      while (m_produced < wanted && !m_exhausted && !m_producerStop)
      {
        // A disc holding a fixed picture until the viewer chooses is saying no more bytes are
        // coming, and a menu drawn over a frozen frame is a very ordinary thing for a DVD to
        // open with. Waiting there means the splitter never finishes scanning, so the graph is
        // never built and the viewer is shown nothing at all -- not even a menu to press a
        // button on. Answer with what the disc has played, which for as long as the still
        // lasts is genuinely the whole stream.
        //
        // Asked before waiting rather than after, because this is the graph's own streaming
        // thread and a second spent waiting on a still already declared is a second of the
        // interface held up for nothing.
        if (m_navigator->HoldingIndefiniteStill())
        {
          CLog::Log(LOGDEBUG,
                    "{} - the disc is holding a still at {} and will play nothing more until the "
                    "viewer chooses, so {} bytes is all there is", __FUNCTION__, m_position,
                    static_cast<LONGLONG>(m_produced));
          break;
        }

        if (!m_ringGrew.wait_for(ring, NAVIGATION_READ_TIMEOUT,
                                 [&] { return m_produced >= wanted || m_exhausted || m_producerStop; }))
          CLog::Log(LOGDEBUG, "{} - still waiting for the disc at {}", __FUNCTION__, m_position);
      }
    }

    if (m_producerStop)
      hr = S_FALSE;
    else
    {
      std::lock_guard<std::mutex> ring(m_ringMutex);
      const DWORD available =
          static_cast<DWORD>(std::min<LONGLONG>(dwBytesToRead, m_produced - m_position));
      if (available > 0)
      {
        CopyFromHistory(m_position, pbBuffer, available);
        m_position += available;
      }

      done = available;
      hr = (available == dwBytesToRead) ? S_OK : S_FALSE;
    }
  }

  if (pdwBytesRead)
    *pdwBytesRead = done;

  const double readMilliseconds =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readStarted)
          .count();

  // The opening exchange with the splitter decides whether it will play the stream at all,
  // and it is short, so trace every read of it. After that only the reads worth explaining:
  // one that took a while, or one that came up short, which is how this interface says the
  // stream has ended.
  if (m_traced < TRACED_READS || readMilliseconds > 100.0 || done < dwBytesToRead)
  {
    m_traced++;
    CLog::Log(LOGDEBUG, "{} - read {} at {} -> {} bytes, hr {:#x}, played {}, took {:.0f} ms",
              __FUNCTION__, dwBytesToRead, asked, done, static_cast<uint32_t>(hr),
              static_cast<LONGLONG>(m_produced), readMilliseconds);
  }
  ReportThroughput(done, readMilliseconds);

  return hr;
}

LONGLONG CDSDvdStream::Size(LONGLONG* pSizeAvailable)
{
  // Deliberately takes no lock. A read holds one while it waits on the disc, and a disc
  // showing a menu can have nothing to give for as long as the viewer takes to choose, so
  // anything answered under that lock would hang for just as long. The interface asks this
  // constantly, and hanging it hangs the application.
  const LONGLONG played = m_produced;

  // A navigated disc has no length: how much it will play depends on what the viewer chooses.
  // Report more to come rather than only what has been played, because what has been played
  // only grows when the splitter reads, and a splitter told there is nothing more to read
  // stops reading. Reads wait for the disc instead.
  //
  // Except while the disc holds a fixed picture: claiming more then sends the splitter hunting
  // near an end that does not exist, and every one of those reads has to be refused.
  const bool nothingMoreComing =
      m_exhausted || (m_navigator && m_navigator->HoldingIndefiniteStill());
  const LONGLONG length = nothingMoreComing ? played : played + NAVIGATION_LOOKAHEAD;

  if (m_traced < TRACED_READS)
    CLog::Log(LOGDEBUG, "{} - reporting length {}, played {}", __FUNCTION__, length, played);

  if (pSizeAvailable)
    *pSizeAvailable = length;

  return length;
}

DWORD CDSDvdStream::Alignment()
{
  return 1;
}

void CDSDvdStream::Lock()
{
  m_lock.lock();
}

void CDSDvdStream::Unlock()
{
  m_lock.unlock();
}

CDSDvdReader::CDSDvdReader(LPUNKNOWN pUnknown, HRESULT* phr)
  : CAsyncReader(NAME("Kodi DVD Reader\0"), pUnknown, &m_stream, phr), m_stream()
{
  // A DVD is an MPEG-2 program stream, where a Blu-ray is a transport stream. The splitter
  // picks its parser from this.
  m_mt.majortype = MEDIATYPE_Stream;
  m_mt.subtype = MEDIASUBTYPE_MPEG2_PROGRAM;
}

HRESULT STDMETHODCALLTYPE CDSDvdReader::Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt)
{
  std::string path;
  g_charsetConverter.wToUTF8(pszFileName, path);

  CLog::Log(LOGINFO, "{} - opening \"{}\"", __FUNCTION__, path.c_str());
  return m_stream.Open(path);
}

STDMETHODIMP CDSDvdReader::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
  CheckPointer(ppv, E_POINTER);

  return (riid == __uuidof(IFileSourceFilter)) ? GetInterface((IFileSourceFilter*)this, ppv)
                                               : __super::NonDelegatingQueryInterface(riid, ppv);
}

#endif
