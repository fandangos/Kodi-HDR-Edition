/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#if HAS_DS_PLAYER

#include "DSBlurayNavigator.h"

#include "DSPlayer.h"
#include "FileItem.h"
#include "ServiceBroker.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlay.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlayImage.h"
#include "cores/VideoPlayer/DVDInputStreams/DVDInputStreamBluray.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "windowing/WinSystem.h"

namespace
{
//! How long to wait each time the disc has nothing to give before asking it again
constexpr auto HOLD_WAIT = std::chrono::milliseconds(20);

//! How many of those waits to sit through before letting the caller have an empty read.
//! A menu can hold its picture indefinitely, so this is not an error, it just puts a bound
//! on how long one read occupies the graph's streaming thread.
constexpr int MAX_HOLD_ATTEMPTS = 10;

//! How often the disc is attended to while a playlist plays directly, see StartPump.
//! Short enough that a button on a popup menu answers at once, long enough that the disc is
//! not being interrupted for nothing tens of times a second.
constexpr auto PUMP_INTERVAL = std::chrono::milliseconds(50);

//! How long a menu may stay on screen with nothing arriving before it is called stuck. Long
//! enough that a viewer reading a menu is not accused of it.
constexpr auto STUCK_MENU_AFTER = std::chrono::seconds(30);

//! Every how many pixels the overlay is sampled when asking whether anything is visible.
//! The smallest thing a disc has ever drawn to mean "a menu is up" is a button, and a button
//! is tens of pixels across in both directions, so looking at one pixel in an 8x8 block
//! cannot miss one -- while reading a sixty-fourth of an eight megabyte frame.
constexpr int VISIBILITY_STRIDE = 8;

//! How opaque a pixel has to be to count as visible. Discs fade menus in and out, and a menu
//! faded almost to nothing is not one the viewer can use.
constexpr uint8_t VISIBLE_ALPHA = 8;

//! \brief The alpha of one pixel of an overlay image, whether paletted or plain ARGB
uint8_t AlphaAt(const CDVDOverlayImage& image, int x, int y)
{
  if (image.palette.empty())
  {
    // Plain ARGB, which is what a BD-J disc draws with
    const size_t offset = static_cast<size_t>(y) * image.linesize + static_cast<size_t>(x) * 4;
    if (offset + 3 >= image.pixels.size())
      return 0;
    return image.pixels[offset + 3];
  }

  // Paletted, which is what an HDMV disc draws with
  const size_t offset = static_cast<size_t>(y) * image.linesize + static_cast<size_t>(x);
  if (offset >= image.pixels.size())
    return 0;

  const uint8_t index = image.pixels[offset];
  if (index >= image.palette.size())
    return 0;

  return static_cast<uint8_t>((image.palette[index] >> 24) & 0xff);
}

/*!
 * \brief Whether an overlay would put anything the viewer can see on the screen
 *
 * The question "is a menu on screen" has no honest answer short of this one. Asking libbluray
 * gets the title's menu code being loaded, which on a BD-J disc is true for the whole film.
 * Asking whether the disc sent an overlay at all is nearly right, and fails on the discs that
 * matter: Mortal Kombat II keeps its graphics plane up for the entire feature and simply
 * empties the pixels when its popup menu goes away, so it never once sends the empty overlay
 * that would mean "menu gone". Only the pixels know.
 */
bool AnythingVisible(const std::shared_ptr<CDVDOverlayGroup>& overlay)
{
  if (!overlay)
    return false;

  for (const auto& element : overlay->m_overlays)
  {
    const auto* image = dynamic_cast<const CDVDOverlayImage*>(element.get());
    if (!image || image->width <= 0 || image->height <= 0 || image->pixels.empty())
      continue;

    for (int y = 0; y < image->height; y += VISIBILITY_STRIDE)
      for (int x = 0; x < image->width; x += VISIBILITY_STRIDE)
        if (AlphaAt(*image, x, y) >= VISIBLE_ALPHA)
          return true;
  }

  return false;
}
} // unnamed namespace

CDSBlurayNavigator* CDSBlurayNavigator::m_instance = nullptr;
std::shared_ptr<CDSBlurayNavigator> CDSBlurayNavigator::m_session;
std::string CDSBlurayNavigator::m_sessionPath;

std::shared_ptr<CDSBlurayNavigator> CDSBlurayNavigator::Session(const std::string& path)
{
  if (m_session && m_sessionPath == path)
  {
    CLog::Log(LOGINFO, "{} - carrying on with the disc already playing", __FUNCTION__);
    return m_session;
  }

  EndSession();

  auto navigator = std::make_shared<CDSBlurayNavigator>();
  if (!navigator->Open(path))
    return nullptr;

  m_session = navigator;
  m_sessionPath = path;
  return m_session;
}

void CDSBlurayNavigator::EndSession()
{
  if (!m_session)
    return;

  CLog::Log(LOGINFO, "{} - finished with the disc", __FUNCTION__);
  m_session->Close();
  m_session.reset();
  m_sessionPath.clear();
}

CDSBlurayNavigator::CDSBlurayNavigator() = default;

CDSBlurayNavigator::~CDSBlurayNavigator()
{
  Close();
}

bool CDSBlurayNavigator::Open(const std::string& path)
{
  std::unique_lock<CCriticalSection> lock(m_lock);

  Close();

  CFileItem item(path, false);
  auto input = std::make_shared<CDVDInputStreamBluray>(this, item);

  if (!input->Open())
  {
    CLog::Log(LOGERROR, "{} - could not open \"{}\" for navigation", __FUNCTION__, path);
    return false;
  }

  // Open() succeeds either way, but only navigation mode presents the disc's menus. The
  // caller wants menus, so a disc that fell back to playing a title straight through is
  // no use to us and is better served by the plain title reader.
  if (input->GetSupportedMenuType() != MenuType::NATIVE)
  {
    CLog::Log(LOGINFO, "{} - \"{}\" cannot be navigated, no menus available", __FUNCTION__, path);
    return false;
  }

  {
    std::unique_lock<CCriticalSection> inputLock(m_inputLock);
    m_input = std::move(input);
  }
  m_produced = 0;
  m_overlayCount = 0;
  m_instance = this;

  CLog::Log(LOGINFO, "{} - navigating \"{}\"", __FUNCTION__, path);
  return true;
}

void CDSBlurayNavigator::Close()
{
  // Before the disc goes, or the pump is left calling into something being torn down
  StopPump();

  if (m_instance == this)
    m_instance = nullptr;

  std::shared_ptr<CDVDInputStreamBluray> input;
  {
    std::unique_lock<CCriticalSection> inputLock(m_inputLock);
    input.swap(m_input);
  }

  if (!input)
    return;

  CLog::Log(LOGINFO, "{} - closing after {} bytes and {} overlay update(s)", __FUNCTION__,
            m_produced, m_overlayCount);

  // A read may still be inside libbluray. Telling it to give up first means Close does not
  // wait on a disc that has stopped producing anything.
  input->Abort();
  input->Close();
  input.reset();
  m_produced = 0;

  // Leave an empty overlay behind so the renderer clears whatever menu was on screen
  std::unique_lock<CCriticalSection> overlayLock(m_overlayLock);
  m_overlay.reset();
  m_overlayPending = true;
  m_menuOnScreen = false;
}

void CDSBlurayNavigator::ClearMenu()
{
  std::unique_lock<CCriticalSection> overlayLock(m_overlayLock);

  if (!m_menuOnScreen && !m_overlay)
    return;

  CLog::Log(LOGDEBUG, "{} - taking the disc's menu off the screen", __FUNCTION__);
  m_overlay.reset();
  m_overlayPending = true;
  m_menuOnScreen = false;
  CDSPlayer::ForgetDiscInputOverride();
}

void CDSBlurayNavigator::StartPump(uint32_t playlistBeingPlayed)
{
  m_playedDirectly = playlistBeingPlayed;

  // The chapter the disc is on now is where the film is about to start from, so it is not
  // something the viewer asked for
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
    m_chapterSeen = input->GetChapter();

  if (m_pump.joinable())
    return;

  m_pumpStop = false;
  m_menuSince = {};
  m_stuckMenuReported = false;
  m_pump = std::thread([this]() { Pump(); });
}

void CDSBlurayNavigator::StopPump()
{
  m_playedDirectly = 0;
  m_pumpStop = true;
  if (m_pump.joinable())
    m_pump.join();
}

void CDSBlurayNavigator::Pump()
{
  CLog::Log(LOGINFO, "{} - attending to the disc while its playlist plays", __FUNCTION__);

  while (!m_pumpStop)
  {
    if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
    {
      std::unique_lock<CCriticalSection> lock(m_discLock);
      input->ProcessEvents();
    }

    // The disc changes programme when its menu is used, which is a thing that happens
    // between events rather than in one of them
    NotePlaylist();

    WatchForAStuckMenu();

    KODI::TIME::Sleep(PUMP_INTERVAL);
  }

  CLog::Log(LOGINFO, "{} - no longer attending to the disc", __FUNCTION__);
}

void CDSBlurayNavigator::WatchForAStuckMenu()
{
  if (!m_menuOnScreen)
  {
    m_menuSince = {};
    m_stuckMenuReported = false;
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  uint64_t overlays = 0;
  {
    std::unique_lock<CCriticalSection> overlayLock(m_overlayLock);
    overlays = m_overlayCount;
  }

  // Restart the clock whenever the menu changes: a viewer moving between buttons produces a
  // new overlay each time, and a menu being used is not a menu that is stuck
  if (m_menuSince == std::chrono::steady_clock::time_point{} || overlays != m_menuSinceOverlay)
  {
    m_menuSince = now;
    m_menuSinceOverlay = overlays;
    m_stuckMenuReported = false;
    return;
  }

  if (m_stuckMenuReported || (now - m_menuSince) < STUCK_MENU_AFTER)
    return;

  m_stuckMenuReported = true;
  CLog::Log(LOGWARNING,
            "{} - the disc has had a menu on screen for {}s without changing it, and the disc "
            "itself says its menu is {}. Every key press is going to the disc; if it is not "
            "answering, the input toggle will hand them back to Kodi.",
            __FUNCTION__, STUCK_MENU_AFTER.count(),
            m_discSaysMenu ? "visible" : "not visible");
}

void CDSBlurayNavigator::SeekPlaybackToChapter(int chapter)
{
  const uint32_t playing = m_playedDirectly;
  if (playing == 0)
    return;

  std::shared_ptr<CDVDInputStreamBluray> input = Input();
  if (!input)
    return;

  // The disc leaving the programme on screen is a change of programme, not a seek within it,
  // and the graph has to be rebuilt for it. NotePlaylist is what notices that.
  if (input->GetPlaylist() != playing)
  {
    CLog::Log(LOGDEBUG,
              "{} - the disc is on chapter {} of playlist {}, but playlist {} is playing, so "
              "this is not a seek", __FUNCTION__, chapter, input->GetPlaylist(), playing);
    return;
  }

  // Seconds from the start of the playlist, which is also where the stream being played
  // begins, because it is that same playlist opened on its own
  const int64_t seconds = input->GetChapterPos(chapter);
  if (seconds < 0)
    return;

  CLog::Log(LOGINFO, "{} - the disc's menu skipped to chapter {}, {} s in", __FUNCTION__, chapter,
            seconds);
  CDSPlayer::NoteDiscWantsSeek(seconds * 1000);
}

int CDSBlurayNavigator::WhereTheDiscsAudioStreamSits(int pid) const
{
  std::shared_ptr<CDVDInputStreamBluray> input = Input();
  return input ? input->AudioStreamIndexByPid(pid) : 0;
}

int CDSBlurayNavigator::WhereTheDiscsSubtitleSits(int pid) const
{
  std::shared_ptr<CDVDInputStreamBluray> input = Input();
  return input ? input->SubtitleStreamIndexByPid(pid) : 0;
}

bool CDSBlurayNavigator::TakeOverlay(std::shared_ptr<CDVDOverlayGroup>& overlay)
{
  std::unique_lock<CCriticalSection> lock(m_overlayLock);

  if (!m_overlayPending)
    return false;

  overlay = m_overlay;
  m_overlayPending = false;
  return true;
}

bool CDSBlurayNavigator::ReleaseHold()
{
  // A hold means the disc changed what it is playing, or put up a still. VideoPlayer
  // answers by throwing its demuxer away and opening a new one on the new stream. We
  // cannot: the splitter downstream owns the demuxer and only ever sees a byte stream, so
  // the hold has to be cleared here and the bytes kept flowing. The splitter picks up the
  // new programme from the stream itself.
  switch (m_input->NextStream())
  {
    case CDVDInputStream::NEXTSTREAM_OPEN:
      m_still = false;
      return true;

    case CDVDInputStream::NEXTSTREAM_RETRY:
      // A still. Skipping it would walk straight past whatever it is holding the picture
      // for, a menu included, so the only still ended early is one the disc gave a time
      // limit to. Menus with moving backgrounds never come through here at all, they are
      // playlists that loop and their holds are the loop points below.
      if (!m_stillIsIndefinite && std::chrono::steady_clock::now() >= m_stillUntil)
      {
        CLog::Log(LOGDEBUG, "{} - timed still expired", __FUNCTION__);
        EndStill();
        return true;
      }

      m_still = true;
      return false;

    case CDVDInputStream::NEXTSTREAM_NONE:
    default:
      m_finished = true;
      return false;
  }
}

void CDSBlurayNavigator::EndStill()
{
  // Harmless when the disc is not holding anything: SkipStill only acts on a still
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
    input->SkipStill();

  m_still = false;
}

int CDSBlurayNavigator::Read(uint8_t* buffer, int size)
{
  std::shared_ptr<CDVDInputStreamBluray> input = Input();
  if (!input)
    return -1;

  for (int attempt = 0; attempt < MAX_HOLD_ATTEMPTS; ++attempt)
  {
    // No lock of ours around this. The call can sit inside libbluray indefinitely and
    // anything waiting on us would wait exactly as long, the interface included.
    const int read = input->Read(buffer, size);
    m_inMenu = input->IsInMenu();

    // libbluray does not call the player back when the playlist changes, so the change is
    // watched for here as well as in the callbacks. A menu over moving video announces
    // itself through overlay callbacks; a menu over a still picture only shows up here,
    // once choosing something makes the disc produce data again.
    NotePlaylist();

    if (read > 0)
    {
      // Whatever the disc was holding for is over. Forgetting the terms of that still
      // matters: without it a later break in the stream, which arrives with no still time
      // attached, would inherit them and wait for a viewer who has nothing to choose.
      m_still = false;
      m_stillIsIndefinite = false;
      m_stillUntil = {};
      m_produced += read;
      return read;
    }

    if (read < 0)
      return read;

    // Nothing came back, so either the disc is between things and needs letting on, or it
    // is deliberately sitting still with nothing to give
    if (ReleaseHold())
      continue;

    if (m_finished)
      return 0;

    // The disc is holding a fixed picture and will send nothing until the viewer chooses.
    // Wait rather than returning straight away, or the graph asks again immediately and the
    // two spin, which burns a core and starves everything else.
    KODI::TIME::Sleep(HOLD_WAIT);
  }

  return 0;
}

bool CDSBlurayNavigator::Finished() const
{
  return m_finished;
}

void CDSBlurayNavigator::AnnounceProgrammeChanges()
{
  m_playlistSeen = Playlist();
  m_announceChanges = true;
}

uint32_t CDSBlurayNavigator::Playlist() const
{
  std::shared_ptr<CDVDInputStreamBluray> input = Input();
  return input ? input->GetPlaylist() : 0;
}

bool CDSBlurayNavigator::InMainMenu() const
{
  std::shared_ptr<CDVDInputStreamBluray> input = Input();
  return input && input->IsInMainMenu();
}

void CDSBlurayNavigator::Abort()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
  {
    CLog::Log(LOGWARNING, "{} - giving up on the disc", __FUNCTION__);
    input->Abort();
  }
}

std::shared_ptr<CDVDInputStreamBluray> CDSBlurayNavigator::Input() const
{
  std::unique_lock<CCriticalSection> lock(m_inputLock);
  return m_input;
}

bool CDSBlurayNavigator::ShowMenu()
{
  std::shared_ptr<CDVDInputStreamBluray> input = Input();
  if (!input)
    return false;

  // A disc sitting in a still is waiting to be let go of before it will act on anything
  EndStill();

  std::unique_lock<CCriticalSection> lock(m_discLock);
  const bool shown = input->OnMenu();
  CLog::Log(LOGDEBUG, "{} - asking the disc for its menu: {}", __FUNCTION__,
            shown ? "accepted" : "refused");
  return shown;
}

void CDSBlurayNavigator::OnBack()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
  {
    std::unique_lock<CCriticalSection> lock(m_discLock);
    input->OnBack();
  }
}

void CDSBlurayNavigator::OnUp()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
  {
    std::unique_lock<CCriticalSection> lock(m_discLock);
    input->OnUp();
  }
}

void CDSBlurayNavigator::OnDown()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
  {
    std::unique_lock<CCriticalSection> lock(m_discLock);
    input->OnDown();
  }
}

void CDSBlurayNavigator::OnLeft()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
  {
    std::unique_lock<CCriticalSection> lock(m_discLock);
    input->OnLeft();
  }
}

void CDSBlurayNavigator::OnRight()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
  {
    std::unique_lock<CCriticalSection> lock(m_discLock);
    input->OnRight();
  }
}

void CDSBlurayNavigator::OnSelect()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
  {
    EndStill();

    {
      std::unique_lock<CCriticalSection> lock(m_discLock);
      input->ActivateButton();
    }

    // Choosing something is what makes the disc change programme, and the events saying so
    // are consumed right here on this thread, with no callback for the playlist itself
    NotePlaylist();
  }
}

void CDSBlurayNavigator::NotePlaylist()
{
  const uint32_t playlist = Playlist();
  if (playlist == m_playlistSeen)
    return;

  // Exchanged rather than assigned: the reading thread, the pump and whichever thread is
  // carrying a key press all come through here, and only one of them should announce a move
  const uint32_t previous = m_playlistSeen.exchange(playlist);
  if (previous == playlist)
    return;

  // Nothing to announce while the disc is still finding its way in and no graph has been
  // built on any of it yet
  if (previous == 0 || !m_announceChanges)
    return;

  CLog::Log(LOGINFO, "{} - the disc moved from playlist {} to {}", __FUNCTION__, previous,
            playlist);

  // A splitter parses its stream once, so it cannot follow the disc across this. Noticed
  // here rather than while reading, because the disc changes programme when the viewer
  // chooses something, which happens on the thread carrying the keypress, and reading may
  // have gone quiet by then.
  CDSPlayer::NoteDiscProgrammeChanged();
}

int CDSBlurayNavigator::OnDiscNavResult(void* pData, int iMessage)
{
  // Any event may be the one that follows a change of programme
  NotePlaylist();

  // Called from inside libbluray's callbacks, so the lock is already held by whoever
  // called into the input stream.
  switch (iMessage)
  {
    case BD_EVENT_MENU_OVERLAY:
    {
      auto* group = static_cast<std::shared_ptr<CDVDOverlayGroup>*>(pData);
      if (!group)
        break;

      std::unique_lock<CCriticalSection> overlayLock(m_overlayLock);
      m_overlay = *group;
      m_overlayPending = true;
      m_overlayCount++;

      // Whether a menu is on screen is this, and only this: whether anything the viewer can
      // see is being drawn. Not whether an overlay arrived -- Mortal Kombat II holds its
      // graphics plane up for the whole film and empties the pixels when its popup menu goes
      // away, so by that test its menu was up from the first frame to the last and the OSD
      // could never be reached again. And not libbluray's own answer either, which is about
      // the title's menu code being loaded and stays true just as long.
      const bool drawing = AnythingVisible(m_overlay);
      if (drawing != m_menuOnScreen)
      {
        CLog::Log(LOGDEBUG, "{} - the disc {} drawing a menu", __FUNCTION__,
                  drawing ? "is now" : "has stopped");
        m_menuOnScreen = drawing;

        // A fresh answer, so any decision the viewer made about the old one has had its day
        CDSPlayer::ForgetDiscInputOverride();
      }

      // Logged sparsely: a moving menu highlight produces one of these per frame
      if (m_overlayCount == 1 || (m_overlayCount % 100) == 0)
      {
        CLog::Log(LOGDEBUG, "{} - overlay update {} carrying {} image(s)", __FUNCTION__,
                  m_overlayCount, m_overlay ? m_overlay->m_overlays.size() : 0);
      }
      break;
    }

    case BD_EVENT_MENU_ERROR:
      CLog::Log(LOGERROR, "{} - the disc reported a menu error", __FUNCTION__);
      break;

    case BD_EVENT_ENC_ERROR:
      CLog::Log(LOGERROR, "{} - the disc is encrypted and cannot be decoded", __FUNCTION__);
      break;

    case BD_EVENT_STILL_TIME:
    {
      // Seconds the disc wants the picture held for, zero meaning until the viewer chooses
      // something, which is how a menu waits
      const int seconds = pData ? *static_cast<int*>(pData) : 0;
      m_stillIsIndefinite = (seconds == 0);
      m_stillUntil = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);

      CLog::Log(LOGDEBUG, "{} - holding the picture {}", __FUNCTION__,
                m_stillIsIndefinite ? "until the viewer chooses"
                                    : std::to_string(seconds) + "s");
      break;
    }

    case BD_EVENT_STILL:
      // Handled where the read holds, nothing to do here
      break;

    case BD_EVENT_PLAYLIST_STOP:
      CLog::Log(LOGDEBUG, "{} - playlist stopped", __FUNCTION__);
      break;

    case BD_EVENT_MENU:
    {
      // The disc's own word on whether its menu is visible. Kept beside the answer taken
      // from the overlays rather than replacing it: a disc says yes for a menu it has built
      // and not drawn, and where the viewer's keys go has to follow what they can see.
      const bool visible = pData && *static_cast<int*>(pData) != 0;
      if (visible != m_discSaysMenu)
      {
        CLog::Log(LOGDEBUG, "{} - the disc says its menu is {} (we can see {})", __FUNCTION__,
                  visible ? "visible" : "not visible", m_menuOnScreen ? "one" : "none");
        m_discSaysMenu = visible;
      }
      break;
    }

    case BD_EVENT_CHAPTER:
    {
      // A menu skipping chapters. The disc moves its own idea of where it is; the film is
      // being played from a separate handle that knows nothing about it, so the move has to
      // be repeated here as an ordinary seek.
      const int chapter = pData ? *static_cast<int*>(pData) : 0;
      if (chapter <= 0 || chapter == m_chapterSeen.exchange(chapter))
        break;

      SeekPlaybackToChapter(chapter);
      break;
    }

    case BD_EVENT_AUDIO_STREAM:
    {
      // The disc names the stream by its pid, which means nothing to a splitter that found
      // the streams for itself. Its place in the clip does: both count them in the order
      // they appear in the programme.
      const int pid = pData ? *static_cast<int*>(pData) : -1;
      const int index = WhereTheDiscsAudioStreamSits(pid);
      if (index > 0 && m_playedDirectly != 0)
      {
        CLog::Log(LOGINFO, "{} - the disc chose audio stream {} (pid {})", __FUNCTION__, index,
                  pid);
        CDSPlayer::NoteDiscWantsAudioStream(index - 1);
      }
      break;
    }

    case BD_EVENT_PG_TEXTST_STREAM:
    {
      const int pid = pData ? *static_cast<int*>(pData) : -1;
      const int index = WhereTheDiscsSubtitleSits(pid);
      if (index > 0 && m_playedDirectly != 0)
      {
        CLog::Log(LOGINFO, "{} - the disc chose subtitle stream {} (pid {})", __FUNCTION__, index,
                  pid);
        CDSPlayer::NoteDiscWantsSubtitleStream(index - 1);
      }
      break;
    }

    case BD_EVENT_PG_TEXTST:
    {
      const bool on = pData && *static_cast<int*>(pData) != 0;
      if (m_playedDirectly != 0)
      {
        CLog::Log(LOGINFO, "{} - the disc turned subtitles {}", __FUNCTION__, on ? "on" : "off");
        CDSPlayer::NoteDiscWantsSubtitles(on);
      }
      break;
    }

    default:
      CLog::Log(LOGDEBUG, "{} - unhandled navigation result {}", __FUNCTION__, iMessage);
      break;
  }

  return 0;
}

void CDSBlurayNavigator::GetVideoResolution(unsigned int& width, unsigned int& height)
{
  // The size libbluray composes menu overlays for. It has to match what the overlays are
  // finally drawn onto, not the disc's video size.
  const RESOLUTION_INFO res = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo();
  width = static_cast<unsigned int>(res.iWidth);
  height = static_cast<unsigned int>(res.iHeight);
}

#endif
