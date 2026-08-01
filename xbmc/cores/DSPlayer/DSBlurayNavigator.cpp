/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#if HAS_DS_PLAYER

#include "DSBlurayNavigator.h"

#include "FileItem.h"
#include "ServiceBroker.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlay.h"
#include "cores/VideoPlayer/DVDInputStreams/DVDInputStreamBluray.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "windowing/WinSystem.h"

namespace
{
//! A disc that neither produces data nor finishes is stuck. Bail out of the read rather
//! than spinning forever on the graph's streaming thread.
constexpr int MAX_HOLD_ATTEMPTS = 32;
} // unnamed namespace

CDSBlurayNavigator* CDSBlurayNavigator::m_instance = nullptr;

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
  auto input = std::make_unique<CDVDInputStreamBluray>(this, item);

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

  m_input = std::move(input);
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

  if (!m_input)
    return;

  CLog::Log(LOGINFO, "{} - closing after {} bytes and {} overlay update(s)", __FUNCTION__,
            m_produced, m_overlayCount);

  m_input->Close();
  m_input.reset();
  m_produced = 0;

  // Leave an empty overlay behind so the renderer clears whatever menu was on screen
  std::unique_lock<CCriticalSection> overlayLock(m_overlayLock);
  m_overlay.reset();
  m_overlayPending = true;
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
      // A still. Skipping it would walk straight past the menu it is holding the picture
      // for, so the only still we end early is one the disc gave a time limit to.
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
  if (!m_still && !m_input)
    return;

  m_input->SkipStill();
  m_still = false;
}

int CDSBlurayNavigator::Read(uint8_t* buffer, int size)
{
  std::unique_lock<CCriticalSection> lock(m_lock);

  if (!m_input)
    return -1;

  for (int attempt = 0; attempt < MAX_HOLD_ATTEMPTS; ++attempt)
  {
    const int read = m_input->Read(buffer, size);
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
    // is deliberately sitting still and there is nothing to wait for
    if (!ReleaseHold())
      return 0;
  }

  CLog::Log(LOGWARNING, "{} - disc produced nothing after {} attempts", __FUNCTION__,
            MAX_HOLD_ATTEMPTS);
  return 0;
}

bool CDSBlurayNavigator::Finished() const
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  return m_finished;
}

bool CDSBlurayNavigator::ShowMenu()
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  return m_input && m_input->OnMenu();
}

bool CDSBlurayNavigator::IsInMenu()
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  return m_input && m_input->IsInMenu();
}

void CDSBlurayNavigator::OnBack()
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  if (m_input)
    m_input->OnBack();
}

void CDSBlurayNavigator::OnUp()
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  if (m_input)
    m_input->OnUp();
}

void CDSBlurayNavigator::OnDown()
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  if (m_input)
    m_input->OnDown();
}

void CDSBlurayNavigator::OnLeft()
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  if (m_input)
    m_input->OnLeft();
}

void CDSBlurayNavigator::OnRight()
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  if (m_input)
    m_input->OnRight();
}

void CDSBlurayNavigator::OnSelect()
{
  std::unique_lock<CCriticalSection> lock(m_lock);
  if (m_input)
    m_input->ActivateButton();
}

int CDSBlurayNavigator::OnDiscNavResult(void* pData, int iMessage)
{
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
