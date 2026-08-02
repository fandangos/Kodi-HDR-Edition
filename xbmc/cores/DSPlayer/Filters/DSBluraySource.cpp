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
#include "cores/DSPlayer/DSPlayer.h"
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
#include <atomic>
#include <thread>

namespace
{
//! Blu-ray aligned unit size. bd_seek() lands on a multiple of this, so a read that
//! starts mid unit has to skip the remainder.
constexpr DWORD BLURAY_UNIT_SIZE = 6144;

//! How far back a navigated stream can be read. Bigger than a moment of parsing needs,
//! because the splitter hunts for the end of the stream by reading near it and backing off
//! in doubling steps, the last of which lands about 32 MB short.
constexpr size_t HISTORY_SIZE = 64 * 1024 * 1024;

//! Bytes pulled from the disc in one go when the history window has to grow
constexpr int PULL_SIZE = 128 * 1024;

//! How far a navigated stream will skip forwards by throwing bytes away. A splitter
//! probing for the end of the file asks for a position far beyond anything the disc has
//! played, and reading the whole way there would burn through the disc.
constexpr LONGLONG MAX_FORWARD_SKIP = 8 * 1024 * 1024;

//! Reported as the distance still to come in navigation mode, where the real length is not
//! known until the disc has finished deciding what to play.
//!
//! Deliberately small. The splitter looks for the end of the stream by reading near the
//! length we claim, so whatever we claim has to be somewhere the disc can actually reach:
//! claiming a length far beyond anything that exists means every one of those reads fails
//! and the splitter gives up on the stream entirely. Kept below MAX_FORWARD_SKIP so that
//! reaching the claimed end is allowed rather than refused.
constexpr LONGLONG NAVIGATION_LOOKAHEAD = 4 * 1024 * 1024;

//! How long a disc gets to start playing something before navigation is abandoned. Long
//! enough for a BD-J disc to start a Java VM and run its menu code.
constexpr std::chrono::milliseconds NAVIGATION_START_TIMEOUT{40000};

//! Longest to spend waiting for the disc to stop changing playlist before taking whatever
//! it is on. Must leave room inside NAVIGATION_START_TIMEOUT for priming afterwards.
constexpr std::chrono::milliseconds NAVIGATION_SETTLE_LIMIT{14000};

//! How long one read will wait for the disc before giving the splitter less than it asked
//! for, which the splitter reads as the end of the stream
constexpr std::chrono::milliseconds NAVIGATION_READ_TIMEOUT{1000};

//! How many of the opening reads to trace
constexpr uint32_t TRACED_READS = 60;

//! How long the disc has to stay on one playlist before its bytes are worth showing the
//! splitter.
//!
//! This has to outlast the transitions a disc makes on its way in, not just the gaps
//! between them. Mortal Kombat II passes through a playlist that sits still for nearly
//! three seconds before moving to its menu, and settling on that one left the splitter
//! parsing a programme the disc had already left, after which it never produced a frame.
constexpr std::chrono::milliseconds NAVIGATION_SETTLE{6000};

//! How long the disc has to stay on one playlist once it says it is showing its top menu.
//! Short, because reaching the menu is the disc telling us it has arrived.
constexpr std::chrono::milliseconds MENU_SETTLE{700};

//! How long the disc has to stay put when the player is following it to what it moved to.
//! Shorter than the first time round, because the choosing has already happened.
constexpr std::chrono::milliseconds RESUME_SETTLE{2500};

//! Most that is kept while waiting for the disc to settle. Only there to stop a disc that
//! reads faster than it plays from running away with the whole playlist.
constexpr LONGLONG SETTLE_CAP = 8 * 1024 * 1024;

//! How much of the settled playlist to have ready before the graph is built. Well inside
//! HISTORY_SIZE, so the splitter can still reach the first byte while it scans.
constexpr LONGLONG PRIME_BYTES = 2 * 1024 * 1024;

//! How far ahead of the splitter the disc is read. Enough that a read never waits, small
//! enough that what the splitter may still ask for stays inside HISTORY_SIZE.
constexpr LONGLONG READAHEAD = 8 * 1024 * 1024;

//! How far the disc is allowed to run before playback starts. Comfortably inside
//! HISTORY_SIZE, so that everything read while the splitter works out what the stream holds
//! is still there when it goes back to the beginning to play it.
constexpr LONGLONG OPENING_BYTES_HELD = 48 * 1024 * 1024;

//! Longest to spend collecting those opening bytes before going with what arrived
constexpr std::chrono::milliseconds PRIME_TIME{4000};

//! Shortest programme worth taking away from the disc and playing on its own.
//!
//! A disc on its way from its menu to a title passes through transit clips a few seconds
//! long, and one of those played directly is a dead end: the clip ends, and the disc -- which
//! only moves while it is read, and in that mode is not being read at all -- never gets to
//! whatever it was leading to. Anything this short is left to navigation, where reading it is
//! what carries the disc on into the real title.
//!
//! Deliberately not dsplayer.mintitlelength. That is the title chooser's idea of a feature,
//! thirty minutes by default, and would send every extra on the disc down a path that has no
//! length and cannot seek.
constexpr std::chrono::seconds TOO_SHORT_TO_PLAY_ALONE{60};

//! Longest playlist that may be given back to the disc after it has already been played.
//!
//! The reactive half of the same problem: a transit clip that got past the length test above
//! anyway still has to not strand the disc, and the way out is to play it again through
//! navigation, where reading it carries the disc on. That replays it from its first frame,
//! which is a fair price for a clip and no price at all for a feature -- there it would start
//! the film over. Well above anything a disc uses to move between titles, well below anything
//! anyone would sit and watch. See HandBackToTheDisc.
constexpr std::chrono::seconds LONGEST_WORTH_REPLAYING{600};
} // unnamed namespace

CDSBlurayStream* CDSBlurayStream::m_feeding = nullptr;
std::atomic<bool> CDSBlurayStream::m_playingPlaylist{false};
std::atomic<bool> CDSBlurayStream::m_backToNavigation{false};

void CDSBlurayStream::StopFeedingTheGraph()
{
  CDSBlurayStream* stream = m_feeding;
  if (!stream)
    return;

  CLog::Log(LOGDEBUG, "{} - no more bytes for the graph, it is being taken down", __FUNCTION__);
  stream->m_producerStop = true;
  stream->m_ringGrew.notify_all();
}

void CDSBlurayStream::FollowTheDisc()
{
  CDSBlurayStream* stream = m_feeding;
  if (!stream)
    return;

  CLog::Log(LOGINFO, "{} - the programme has ended, following the disc to the next one",
            __FUNCTION__);
  stream->m_followDisc = true;
}

void CDSBlurayStream::NoteGraphBuilt()
{
  CDSBlurayStream* stream = m_feeding;
  if (!stream || !stream->m_holdingOpeningBytes)
    return;

  CLog::Log(LOGINFO, "{} - the graph is built, the disc may run on from its opening bytes",
            __FUNCTION__);
  stream->m_holdingOpeningBytes = false;
}

void CDSBlurayStream::NoteGraphRunning()
{
  CDSBlurayStream* stream = m_feeding;
  if (!stream || !stream->m_holdingOpeningBytes)
    return;

  CLog::Log(LOGINFO, "{} - playback has started, the disc may run on from its opening bytes",
            __FUNCTION__);
  stream->m_holdingOpeningBytes = false;
}

bool CDSBlurayStream::HandBackToTheDisc()
{
  CDSBlurayStream* stream = m_feeding;
  if (!stream || !m_playingPlaylist)
    return false;

  CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get();
  if (!navigator)
    return false;

  // A disc that has already moved knew this end was coming and has chosen what follows; the
  // rebuild for that is on its way through NotePlaylist. Only a disc still sitting on the
  // playlist that just ran out is stranded.
  if (navigator->Playlist() != stream->m_playlist)
    return false;

  // Handing back means the disc plays this playlist again, through its own navigation, from
  // wherever it stands -- which is its first frame, because it was never read. For the clip
  // this is here to rescue that costs a few seconds and is worth it. For a feature it would
  // mean starting the film over, which is far worse than the still picture the viewer gets
  // by doing nothing, so a programme long enough to be one is left alone and said so in the
  // log. What is left of that case -- getting a disc to its end sequence after a feature it
  // did not read -- wants the navigation session moved to the end of the playlist, which is
  // a separate piece of work.
  const int seconds = navigator->PlaylistDuration() / 1000;
  if (seconds <= 0 || seconds > LONGEST_WORTH_REPLAYING.count())
  {
    CLog::Log(LOGINFO,
              "{} - playlist {} has run out and the disc is still on it, but it runs {} s and "
              "handing it back would play it again from the start", __FUNCTION__,
              stream->m_playlist, seconds);
    return false;
  }

  CLog::Log(LOGINFO,
            "{} - playlist {} has run out after {} s and the disc is still on it. Nothing has "
            "been reading the disc, and a disc only moves while it is read, so it cannot reach "
            "whatever comes next by itself: handing it back to its own navigation",
            __FUNCTION__, stream->m_playlist, seconds);

  m_backToNavigation = true;
  return true;
}

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
  // Stop reading, but leave the disc alone: this also runs when the graph is being rebuilt
  // because the disc moved to another programme, and giving up on the disc there would end
  // the very thing being followed. Reads are bounded, so the thread comes back on its own.
  if (m_feeding == this)
  {
    m_feeding = nullptr;
    m_playingPlaylist = false;
  }

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

  // Open() closes before it opens, so these have to come back to what they mean for a stream
  // that has not started yet, or the next disc inherits the last one's graph
  m_holdingOpeningBytes = true;
  m_followDisc = false;

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

  if (mode != BD_PLAYBACK_MAIN_TITLE)
  {
    // A playlist the disc is to play through its own navigation, rather than being taken away
    // and played on its own. Told to OpenNavigation so it does not spend time settling on a
    // disc the choosing has already been done on.
    uint32_t following = 0;

    // A disc that is already running has been through its menus and chosen what to play.
    // What it chose is an ordinary playlist, and read as one it has a length and can be
    // seeked, which is what a splitter needs to play a whole film. Navigation output is
    // sequential and endless-looking, which is right for a menu and wrong for a feature.
    if (CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get())
    {
      const uint32_t playlist = navigator->Playlist();

      // A playlist that stranded the disc last time goes back to navigation whatever its
      // length says, see HandBackToTheDisc
      const bool handedBack = m_backToNavigation.exchange(false);

      if (playlist > 0 && !navigator->InMainMenu())
      {
        // Not everything a disc leaves its menu for is a programme. It passes through transit
        // clips on the way, and one of those played on its own is a dead end -- the clip runs
        // out and the disc, which is no longer being read, stays exactly where it was. Left to
        // navigation it plays and the disc carries straight on into the title it was leading
        // to, which the existing rebuild path then picks up.
        const int seconds = navigator->PlaylistDuration() / 1000;
        const bool tooShort = seconds > 0 && seconds < TOO_SHORT_TO_PLAY_ALONE.count();

        if (handedBack || tooShort)
        {
          CLog::Log(LOGINFO, "{} - letting the disc play playlist {} itself: it runs {} s, and "
                             "{}", __FUNCTION__, playlist, seconds,
                    handedBack ? "playing it on its own left the disc stranded"
                               : "that is too short to be a programme of its own");
          following = playlist;
        }
        else
        {
          CLog::Log(LOGINFO, "{} - the disc has settled on playlist {}, playing it directly",
                    __FUNCTION__, playlist);
          if (SUCCEEDED(OpenPlaylist(path, playlist)))
            return S_OK;

          CLog::Log(LOGWARNING, "{} - playlist {} could not be played on its own, staying with "
                                "the disc's own navigation", __FUNCTION__, playlist);
        }
      }
    }

    if (OpenNavigation(path, following))
      return S_OK;
  }

  return OpenTitle(path);
#endif
}

bool CDSBlurayStream::OpenNavigation(const std::string& path, uint32_t following)
{
  // The disc may already be playing, if this graph is being built again because the disc
  // moved from its menu to a title. In that case carry on from where it is rather than
  // starting the disc over.
  const bool resuming = (CDSBlurayNavigator::Get() != nullptr);

  std::shared_ptr<CDSBlurayNavigator> navigator = CDSBlurayNavigator::Session(path);
  if (!navigator)
    return false;

  // From here the disc's own output is what plays, and reading it is all the attention it
  // needs. A second thread inside libbluray at the same time is not.
  navigator->StopPump();

  m_history.assign(HISTORY_SIZE, 0);
  m_historyBytes = 0;
  m_historyEnd = 0;
  m_produced = 0;
  m_exhausted = false;
  m_navigator = std::move(navigator);

  // The splitter inspects the opening bytes before it will connect, so the disc has to have
  // started playing something before the graph is built. Give it a moment: a disc spends
  // the first instant loading its menu code, and BD-J discs start a Java VM to do it.
  //
  // The waiting cannot be done by watching the clock here, because a disc whose menu failed
  // to start never returns from the read at all. It takes another thread to call time on it.
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

  // A disc runs through several playlists on its way in: a logo, a warning, then whatever
  // it settles on, often its menu. Each is a different programme, and the splitter is given
  // one stream and reads it as one file, so the bytes it is shown have to belong to a
  // single one of them. Play until the disc stops changing its mind, throwing away what
  // belongs to the playlists it passed through, and present what it settles on from its
  // first byte.
  uint32_t playlist = m_navigator->Playlist();
  const auto waitingSince = std::chrono::steady_clock::now();
  auto settledAt = waitingSince;

  // None of this applies when the disc is already playing and only the graph is being
  // rebuilt: the disc has already chosen, and what it is playing now is exactly what the
  // splitter should be shown.
  while (!m_exhausted)
  {
    // Keep reading the whole time. A disc only advances while it is being read, so pausing
    // here stops the menu: its background stops playing and whatever runs it decides nothing
    // is happening and moves on to something else. Bytes beyond what is worth keeping are
    // thrown away instead.
    if (Pull() == 0)
      KODI::TIME::Sleep(10ms);

    if (m_produced > SETTLE_CAP)
      Discard();

    // Nothing to settle when the caller has already looked at the disc and decided that what
    // it is on is what should play. Settling means reading until the disc stops changing its
    // mind, and a disc is read many times faster than it plays: on a transit clip a few
    // seconds long, waiting would throw away most of it before a frame was ever shown.
    if (following != 0 && m_navigator->Playlist() == following && m_produced >= PRIME_BYTES)
    {
      CLog::Log(LOGDEBUG, "{} - playing playlist {} through the disc's own navigation, as asked",
                __FUNCTION__, following);
      playlist = following;
      break;
    }

    const uint32_t current = m_navigator->Playlist();
    if (current != playlist)
    {
      CLog::Log(LOGDEBUG, "{} - the disc moved from playlist {} to {}", __FUNCTION__, playlist,
                current);

      // What was collected belongs to the programme the disc has just left
      playlist = current;
      settledAt = std::chrono::steady_clock::now();
      Discard();
    }

    const auto now = std::chrono::steady_clock::now();

    // Waiting for the disc to go quiet is guesswork, and guessing wrong means handing the
    // splitter a programme the disc is about to leave. The disc says where it was heading:
    // once it announces its top menu, that is what to show.
    if (m_navigator->InMainMenu() && now - settledAt >= MENU_SETTLE)
    {
      CLog::Log(LOGDEBUG, "{} - the disc reached its top menu on playlist {}", __FUNCTION__,
                playlist);
      break;
    }

    // A disc being followed to what it just moved to has already chosen; it only needs long
    // enough to stop passing through the short playlists on the way there.
    if (now - settledAt >= (resuming ? RESUME_SETTLE : NAVIGATION_SETTLE))
      break;

    // Some discs never stop moving between playlists, and waiting for a quiet moment that
    // never comes is worse than taking whatever is playing now
    if (now - waitingSince >= NAVIGATION_SETTLE_LIMIT)
    {
      CLog::Log(LOGDEBUG, "{} - the disc is still moving about after {}s, taking playlist {}",
                __FUNCTION__, NAVIGATION_SETTLE_LIMIT.count() / 1000, playlist);
      break;
    }

    // A disc read from a local file runs far faster than it plays, and every byte read here
    // is a byte of the menu played and thrown away
    KODI::TIME::Sleep(5ms);
  }

  // Start the stream the splitter will parse from nothing, so its first byte is the first
  // byte of the playlist the disc settled on, and so the whole of it stays inside the window
  // for as long as the splitter is scanning it.
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

  CLog::Log(LOGDEBUG, "{} - playlist {} gave {} bytes to open with", __FUNCTION__, playlist,
            static_cast<LONGLONG>(m_produced));

  started = true;
  watchdog.join();

  m_playlist = playlist;

  if (m_produced == 0)
  {
    CLog::Log(LOGERROR, "{} - \"{}\" navigated but produced nothing to play", __FUNCTION__, path);
    m_navigator.reset();
    return false;
  }

  CLog::Log(LOGINFO, "{} - the disc started playing after {} bytes", __FUNCTION__, m_produced);

  // From here the disc is read on its own thread, so that a read never waits on it
  m_producerStop = false;
  m_feeding = this;
  // What plays now is the disc's own output. Close() only clears this for the stream that was
  // feeding the graph, and the stream being replaced here is a different object, so a
  // navigation graph following a directly played playlist would otherwise inherit its answer.
  m_playingPlaylist = false;
  m_producer = std::thread(&CDSBlurayStream::Produce, this);

  // From here on, a change of programme means the graph has to be built again
  m_navigator->AnnounceProgrammeChanges();

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

HRESULT CDSBlurayStream::OpenPlaylist(const std::string& kodiPath, uint32_t playlist)
{
#if !defined(HAVE_LIBBLURAY)
  return E_FAIL;
#else
  // libbluray opens the file itself here, so it needs a path Windows understands
  std::string path = CDSFile::SmbToUncPath(kodiPath);
  StringUtils::Replace(path, "/", "\\");

  m_bd = bd_open(path.c_str(), nullptr);
  if (!m_bd)
  {
    CLog::Log(LOGERROR, "{} - libbluray could not open \"{}\"", __FUNCTION__, path.c_str());
    return E_FAIL;
  }

  if (bd_select_playlist(m_bd, playlist) <= 0)
  {
    CLog::Log(LOGERROR, "{} - playlist {} cannot be played on its own", __FUNCTION__, playlist);
    Close();
    return E_FAIL;
  }

  m_length = static_cast<LONGLONG>(bd_get_title_size(m_bd));
  m_position = 0;
  m_bdPosition = 0;

  if (m_length <= 0)
  {
    CLog::Log(LOGERROR, "{} - playlist {} has no length", __FUNCTION__, playlist);
    Close();
    return E_FAIL;
  }

  // What is on screen from here on is the film. The disc was left mid-menu and is no longer
  // being read, so it cannot clear that menu itself: say so on its behalf, or it stays drawn
  // over the film and every key press goes on being treated as menu navigation.
  m_playlist = playlist;
  m_playingPlaylist = true;
  m_feeding = this;
  if (CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get())
  {
    navigator->ClearMenu();

    // Nothing reads the disc from here on, and a disc nobody reads never acts on anything.
    // It will still draw a popup menu over the film, so without this the viewer gets a menu
    // whose buttons do nothing and which never comes down again.
    navigator->StartPump(playlist);

    // Announcing was switched off while the graph was being rebuilt, so the playlists the
    // disc passed through on its way here were not each taken for another request. It is
    // here now, and the next move it makes is a real one -- a popup menu sending the viewer
    // back to the top menu, or on to another title -- so start listening again. Without
    // this the disc would be pumped, act on its menu, change programme, and be ignored.
    navigator->AnnounceProgrammeChanges();
  }

  CLog::Log(LOGINFO, "{} - playing playlist {} directly, {} bytes", __FUNCTION__, playlist,
            m_length);
  return S_OK;
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

  std::lock_guard<std::mutex> ring(m_ringMutex);

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

  // Once the stream has been handed over, a change of playlist means the bytes from here on
  // belong to a different programme than the ones the splitter parsed. It has no way to be
  // told that, so note it: rebuilding the graph at this point is the next thing needed.
  // Whether the disc has moved to another programme is noticed by the navigator, which
  // sees the events wherever they arrive, including on the thread carrying a keypress.

  return static_cast<DWORD>(read);
}

void CDSBlurayStream::Produce()
{
  while (!m_producerStop && !m_exhausted)
  {
    // Keep a little ahead of where the splitter is reading, no more. Running further ahead
    // would push the bytes it may still ask for out of the far end of the window. The one
    // exception is when the splitter has stopped reading because it believes its programme
    // has ended: the disc has to be kept moving or it never reaches whatever comes next,
    // and the change of playlist that follows is what triggers the graph being built again.
    if (m_produced - m_position > READAHEAD && !m_followDisc)
    {
      KODI::TIME::Sleep(5ms);
      continue;
    }

    // While the splitter is still scanning, hold the disc near where it started. A splitter
    // reads forward through the stream to work out what is in it and then goes back to the
    // beginning to play it, and a navigated disc cannot be rewound: reading on while it
    // scans pushed the opening bytes out of the window, and when it came back for them
    // there was nothing to give it, so the graph never started.
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

void CDSBlurayStream::Discard()
{
  m_historyBytes = 0;
  m_historyEnd = 0;
  m_produced = 0;
  m_position = 0;
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
  // gets there. Worth knowing about: it costs disc reads and it moves the disc on.
  if (m_position > m_produced)
  {
    CLog::Log(LOGDEBUG, "{} - playing forward {} bytes to reach {}", __FUNCTION__,
              m_position - m_produced, m_position);
  }

  // Wait for the producer to have the bytes, never for the disc itself. A short read is how
  // this interface says end of file, and a disc between one thing and the next is not at its
  // end, so it is worth waiting a little; but the wait releases the ring, so nothing else is
  // held up by it.
  {
    // Wait for the bytes with no deadline of our own. A short read is how this interface
    // says the stream has ended, and a menu holding its picture is not the end of anything:
    // ending the stream there had the player stop playback in the middle of every menu.
    // Blocking here is safe, this is the graph's own streaming thread, and teardown
    // interrupts the wait through m_producerStop.
    const LONGLONG wanted = m_position + static_cast<LONGLONG>(dwBytesToRead);

    // While the disc is being held near its start, bytes beyond what is held are never going
    // to arrive, so waiting for them would wait for ever. Saying the stream ends there is
    // what stops the splitter scanning further, which is the point of holding it.
    //
    // Only while it is scanning, though. A short read is how this interface says the stream
    // has ended, and once the splitter is demuxing there is no taking that back: this is what
    // stopped the top menu after one pass. The hold used to last until playback started, and
    // in the seconds between the graph being built and madVR being ready to run it the
    // splitter read its way to the end of the hold, took that for the end of the film, and
    // delivered end of stream -- so the disc went on looping its menu for ever with nothing
    // consuming it and the picture frozen on the last frame it drew. The hold now ends when
    // the graph does, see NoteGraphBuilt, and this can only be reached while the splitter is
    // still scanning, where nothing has been played and it simply reads again.
    if (m_holdingOpeningBytes && wanted > OPENING_BYTES_HELD)
    {
      if (pdwBytesRead)
        *pdwBytesRead = 0;
      return S_FALSE;
    }

    std::unique_lock<std::mutex> ring(m_ringMutex);
    while (m_produced < wanted && !m_exhausted && !m_producerStop)
    {
      if (!m_ringGrew.wait_for(ring, NAVIGATION_READ_TIMEOUT,
                               [&] { return m_produced >= wanted || m_exhausted || m_producerStop; }))
        CLog::Log(LOGDEBUG, "{} - still waiting for the disc at {}", __FUNCTION__, m_position);
    }
  }

  if (m_producerStop)
    return S_FALSE;

  std::lock_guard<std::mutex> ring(m_ringMutex);
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

  const LONGLONG asked = m_position;

  DWORD done = 0;
  const HRESULT hr = m_navigator ? ReadNavigation(pbBuffer, dwBytesToRead, &done)
                                 : ReadTitle(pbBuffer, dwBytesToRead, &done);

  if (pdwBytesRead)
    *pdwBytesRead = done;

  const double readMilliseconds =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readStarted)
          .count();

  // The opening exchange with the splitter decides whether it will play the stream at all,
  // and it is short, so trace every read of it. After that only the reads worth explaining:
  // one that took a while, or one that came up short, which is how this interface says the
  // stream has ended.
  if (m_navigator && (m_traced < TRACED_READS || readMilliseconds > 100.0 || done < dwBytesToRead))
  {
    m_traced++;
    CLog::Log(LOGDEBUG, "{} - read {} at {} -> {} bytes, hr {:#x}, played {}, took {:.0f} ms",
              __FUNCTION__, dwBytesToRead, asked, done, static_cast<uint32_t>(hr), m_produced,
              readMilliseconds);
  }
  ReportThroughput(done, readMilliseconds);

  return hr;
}

LONGLONG CDSBlurayStream::Size(LONGLONG* pSizeAvailable)
{
  // Deliberately takes no lock. A read holds one while it waits on the disc, and a disc
  // showing a menu can have nothing to give for as long as the viewer takes to choose, so
  // anything answered under that lock would hang for just as long. The interface asks this
  // constantly, and hanging it hangs the application.
  if (m_navigator)
  {
    // A navigated disc has no length: how much it will play depends on what the viewer
    // chooses. Report more to come rather than only what has been played, because what has
    // been played only grows when the splitter reads, and a splitter told there is nothing
    // more to read stops reading. Reads wait for the disc instead.
    const LONGLONG played = m_produced;
    const LONGLONG length = m_exhausted ? played : played + NAVIGATION_LOOKAHEAD;

    if (m_traced < TRACED_READS)
      CLog::Log(LOGDEBUG, "{} - reporting length {}, played {}", __FUNCTION__, length, m_produced);

    if (pSizeAvailable)
      *pSizeAvailable = length;

    return length;
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
