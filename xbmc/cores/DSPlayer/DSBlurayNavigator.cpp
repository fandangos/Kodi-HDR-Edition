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

  const bool shown = input->OnMenu();
  CLog::Log(LOGDEBUG, "{} - asking the disc for its menu: {}", __FUNCTION__,
            shown ? "accepted" : "refused");
  return shown;
}

void CDSBlurayNavigator::OnBack()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
    input->OnBack();
}

void CDSBlurayNavigator::OnUp()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
    input->OnUp();
}

void CDSBlurayNavigator::OnDown()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
    input->OnDown();
}

void CDSBlurayNavigator::OnLeft()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
    input->OnLeft();
}

void CDSBlurayNavigator::OnRight()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
    input->OnRight();
}

void CDSBlurayNavigator::OnSelect()
{
  if (std::shared_ptr<CDVDInputStreamBluray> input = Input())
  {
    EndStill();
    input->ActivateButton();

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

  const uint32_t previous = m_playlistSeen;
  m_playlistSeen = playlist;

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

      // Whether a menu is on screen is this, and only this: whether the disc is currently
      // asking for anything to be drawn. The disc replaces its overlay outright and clears
      // a menu by sending an empty one. Asking libbluray instead answers yes for as long as
      // the title's menu code is loaded, which on a BD-J disc is the whole film.
      const bool drawing = m_overlay && !m_overlay->m_overlays.empty();
      if (drawing != m_menuOnScreen)
      {
        CLog::Log(LOGDEBUG, "{} - the disc {} drawing a menu", __FUNCTION__,
                  drawing ? "is now" : "has stopped");
        m_menuOnScreen = drawing;
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

    case BD_EVENT_AUDIO_STREAM:
    case BD_EVENT_PG_TEXTST:
    case BD_EVENT_PG_TEXTST_STREAM:
      // The disc is asking for a particular stream. The splitter chooses streams itself
      // today; honouring the disc's choice needs the stream manager and comes later.
      break;

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
