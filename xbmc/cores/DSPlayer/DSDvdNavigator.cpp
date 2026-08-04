/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#if HAS_DS_PLAYER

#include "DSDvdNavigator.h"

#include "DSPlayer.h"
#include "FileItem.h"
#include "ServiceBroker.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlay.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlaySpu.h"
#include "cores/DSPlayer/Filters/DSDvdSource.h"
#include "cores/VideoPlayer/DVDInputStreams/DVDInputStreamNavigator.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "windowing/WinSystem.h"

namespace
{
//! How long to wait each time the disc has nothing to give before asking it again
constexpr auto HOLD_WAIT = std::chrono::milliseconds(20);

//! How many of those waits to sit through before letting the caller have an empty read. A
//! menu can hold its picture indefinitely, so this is not an error, it only bounds how long
//! one read occupies the graph's streaming thread.
constexpr int MAX_HOLD_ATTEMPTS = 10;

//! libdvdnav's own value for a still that lasts until the viewer chooses something
constexpr uint8_t STILL_FOREVER = 0xff;

//! A DVD pack is always exactly this, and a PES packet starts inside one
constexpr size_t DVD_PACK_SIZE = 2048;
//! Where the PES packet begins, after the 14 byte pack header
constexpr size_t PACK_HEADER_SIZE = 14;
//! Private stream 1 carries subpicture, and each subpicture stream is 0x20 plus its number
constexpr uint8_t PRIVATE_STREAM_1 = 0xbd;
constexpr uint8_t SUBPICTURE_BASE = 0x20;
constexpr uint8_t SUBPICTURE_TOP = 0x3f;
} // unnamed namespace

CDSDvdNavigator* CDSDvdNavigator::m_instance = nullptr;
std::shared_ptr<CDSDvdNavigator> CDSDvdNavigator::m_session;
std::string CDSDvdNavigator::m_sessionPath;

std::shared_ptr<CDSDvdNavigator> CDSDvdNavigator::Session(const std::string& path)
{
  if (m_session && m_sessionPath == path)
  {
    CLog::Log(LOGINFO, "{} - carrying on with the disc already playing", __FUNCTION__);
    return m_session;
  }

  EndSession();

  auto navigator = std::make_shared<CDSDvdNavigator>();
  if (!navigator->Open(path))
    return nullptr;

  m_session = navigator;
  m_sessionPath = path;
  return m_session;
}

void CDSDvdNavigator::EndSession()
{
  if (!m_session)
    return;

  CLog::Log(LOGINFO, "{} - finished with the disc", __FUNCTION__);
  m_session->Close();
  m_session.reset();
  m_sessionPath.clear();
}

CDSDvdNavigator::CDSDvdNavigator() = default;

CDSDvdNavigator::~CDSDvdNavigator()
{
  Close();
}

bool CDSDvdNavigator::Open(const std::string& path)
{
  Close();

  // IsDiscImage() is what sends CDVDInputStreamNavigator down its stream-callback path, where
  // the image is read through Kodi's virtual file system rather than opened as a device. That
  // is the whole reason an ISO on a network share works here without being mounted, and it is
  // decided from the file item, so the item has to be built from the path Kodi uses.
  CFileItem item(path, false);
  auto input = std::make_shared<CDVDInputStreamNavigator>(this, item);

  if (!input->Open())
  {
    CLog::Log(LOGERROR, "{} - could not open \"{}\" for navigation", __FUNCTION__, path);
    return false;
  }

  {
    std::unique_lock<CCriticalSection> inputLock(m_inputLock);
    m_input = std::move(input);
  }
  m_produced = 0;
  m_finished = false;
  m_titleSeen = -1;
  m_instance = this;
  NowPlaying(this);

  CLog::Log(LOGINFO, "{} - navigating \"{}\"", __FUNCTION__, path);
  return true;
}

void CDSDvdNavigator::Close()
{
  if (m_instance == this)
    m_instance = nullptr;
  if (Playing() == this)
    NowPlaying(nullptr);

  std::shared_ptr<CDVDInputStreamNavigator> input;
  {
    std::unique_lock<CCriticalSection> inputLock(m_inputLock);
    input.swap(m_input);
  }

  if (!input)
    return;

  CLog::Log(LOGINFO, "{} - closing after {} bytes", __FUNCTION__, m_produced);

  // A read may still be inside libdvdnav. CDVDInputStreamNavigator does not override Abort,
  // so unlike the Blu-ray case this does not cut a read short -- what bounds the wait is
  // Read() giving up after MAX_HOLD_ATTEMPTS rather than sitting on a quiet disc for ever.
  input->Abort();
  input->Close();
  input.reset();

  m_produced = 0;
  m_inMenu = false;
  m_still = false;
  m_stillIsIndefinite = false;

  // Leave an empty overlay behind so the renderer clears whatever menu was on screen
  ClearMenu();
  {
    std::unique_lock<CCriticalSection> overlayLock(m_overlayLock);
    m_overlayPending = true;
  }
}

std::shared_ptr<CDVDInputStreamNavigator> CDSDvdNavigator::Input() const
{
  std::unique_lock<CCriticalSection> lock(m_inputLock);
  return m_input;
}

int CDSDvdNavigator::Read(uint8_t* buffer, int size)
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  if (!input)
    return -1;

  for (int attempt = 0; attempt < MAX_HOLD_ATTEMPTS; ++attempt)
  {
    // No lock of ours around this. The call can sit inside libdvdnav indefinitely and
    // anything waiting on us would wait exactly as long, the interface included.
    const int read = input->Read(buffer, size);
    const bool wasInMenu = m_inMenu;
    m_inMenu = input->IsInMenu();

    // The menu's buttons are inside these very bytes, so they are picked out on the way past
    if (read > 0)
      CollectSubpicture(buffer, static_cast<size_t>(read));

    // A disc leaving its menus takes its menu picture with it. It never says so -- the
    // subpicture simply stops arriving -- so the going has to be noticed rather than waited
    // for, or the last menu drawn stays over the film for ever.
    if (wasInMenu && !m_inMenu)
      ClearMenu();

    // A disc changes programme when the viewer chooses something, and that happens on the
    // thread carrying the key press rather than in any event seen here -- so the change is
    // watched for on every read as well as in the callbacks.
    NoteTitle();

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

    // Nothing came back, so either the disc is between things and needs letting on, or it is
    // deliberately sitting still with nothing to give
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

bool CDSDvdNavigator::ReleaseHold()
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  if (!input)
    return false;

  // A hold means libdvdnav wants the demuxer flushed before it goes on: a new video title
  // set, a discontinuity, or a still. VideoPlayer answers by throwing its demuxer away and
  // opening a new one. We cannot -- the splitter downstream owns the demuxer and only ever
  // sees a byte stream -- so the hold is cleared here and the bytes kept flowing. NextStream
  // is what lifts it, which is the same call CDSBlurayNavigator uses for the same purpose.
  switch (input->NextStream())
  {
    case CDVDInputStream::NEXTSTREAM_OPEN:
      // A new title set. The stream really has changed, and the graph is rebuilt for it by
      // the change of title NoteTitle reports; the bytes may keep coming meanwhile.
      m_still = false;
      return true;

    case CDVDInputStream::NEXTSTREAM_RETRY:
      // A still. Skipping it would walk straight past whatever it is holding the picture
      // for, a menu included, so the only still ended early is one the disc gave a time
      // limit to.
      if (!m_stillIsIndefinite && m_still &&
          std::chrono::steady_clock::now() >= m_stillUntil)
      {
        CLog::Log(LOGDEBUG, "{} - timed still expired", __FUNCTION__);
        EndStill();
        return true;
      }

      // Not a still at all, just a hold with nothing behind it: let the disc on
      if (!m_still)
        return true;

      return false;

    case CDVDInputStream::NEXTSTREAM_NONE:
    default:
      m_finished = true;
      return false;
  }
}

void CDSDvdNavigator::CollectSubpicture(const uint8_t* block, size_t length)
{
  // Only while a menu is up. The subpicture stream carries the disc's buttons in a menu and
  // its ordinary subtitles in a film, and they are the same stream -- so decoding it during
  // the feature drew the film's subtitles into the menu layer, where they arrived with no
  // timing of their own and simply stayed on the picture. Subtitles during playback belong to
  // the subtitle filter, exactly as they do for any other file.
  if (!m_inMenu)
    return;

  // Whole packs only. libdvdnav deals in them and a PES packet never straddles two, so
  // anything else is not a block off the disc.
  if (length < DVD_PACK_SIZE || (length % DVD_PACK_SIZE) != 0)
    return;

  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  if (!input)
    return;

  // Only the stream the disc has chosen. A disc may carry a dozen subpicture streams -- one
  // per language, plus letterboxed variants -- and decoding all of them into one menu would
  // draw several languages' buttons on top of each other.
  const int wanted = input->GetActiveSubtitleStream();

  for (size_t pack = 0; pack + DVD_PACK_SIZE <= length; pack += DVD_PACK_SIZE)
  {
    const uint8_t* p = block + pack;

    // Pack header, then PES packets until the pack runs out
    if (p[0] != 0x00 || p[1] != 0x00 || p[2] != 0x01 || p[3] != 0xba)
      continue;

    size_t at = PACK_HEADER_SIZE;

    // The pack header carries a stuffing length in its low three bits
    at += (p[13] & 0x07);

    while (at + 9 <= DVD_PACK_SIZE)
    {
      if (p[at] != 0x00 || p[at + 1] != 0x00 || p[at + 2] != 0x01)
        break;

      const uint8_t streamId = p[at + 3];
      const size_t packetLength = (static_cast<size_t>(p[at + 4]) << 8) | p[at + 5];
      if (packetLength == 0 || at + 6 + packetLength > DVD_PACK_SIZE)
        break;

      if (streamId == PRIVATE_STREAM_1)
      {
        const size_t headerLength = p[at + 8];
        const size_t payload = at + 9 + headerLength;

        if (payload < DVD_PACK_SIZE)
        {
          const uint8_t substream = p[payload];
          if (substream >= SUBPICTURE_BASE && substream <= SUBPICTURE_TOP &&
              (substream - SUBPICTURE_BASE) == wanted)
          {
            // Past the substream byte is the subpicture data itself
            const size_t from = payload + 1;
            const size_t to = at + 6 + packetLength;
            if (to > from && to <= DVD_PACK_SIZE)
            {
              // The demuxer answers with a picture only once it has a whole one, which
              // usually takes several packets
              std::shared_ptr<CDVDOverlaySpu> picture =
                  m_spu.AddData(const_cast<uint8_t*>(p + from), static_cast<int>(to - from), 0.0);

              if (picture)
              {
                m_menu = std::move(picture);
                // A new picture means the highlight has to go on again: the colours belong
                // to the button, and the button belongs to the disc, not to the picture
                m_buttonDrawn = -1;
                PublishMenu();
              }
            }
          }
        }
      }

      at += 6 + packetLength;
    }
  }

  // The picture may stand while the viewer moves between buttons, and only the highlight
  // changes. Noticed here because a key press is acted on by the disc, not announced by it.
  if (m_menu && input->GetCurrentButton() != m_buttonDrawn)
    PublishMenu();
}

void CDSDvdNavigator::PublishMenu()
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  if (!input || !m_menu)
    return;

  // The picture is shared with whatever the renderer already holds, so the highlight goes on
  // a copy: changing the one being drawn from under the render thread is a race, and a menu
  // redraws every time the viewer moves.
  auto picture = std::make_shared<CDVDOverlaySpu>(*m_menu);

  m_buttonDrawn = input->GetCurrentButton();
  input->GetCurrentButtonInfo(*picture, &m_spu, LIBDVDNAV_BUTTON_NORMAL);

  auto group = std::make_shared<CDVDOverlayGroup>();
  group->m_overlays.push_back(picture);

  std::unique_lock<CCriticalSection> overlayLock(m_overlayLock);
  m_overlay = std::move(group);
  m_overlayPending = true;
  m_overlayCount++;

  // Sparsely: a menu redraws whenever the selection moves
  if (m_overlayCount == 1 || (m_overlayCount % 50) == 0)
  {
    CLog::Log(LOGDEBUG, "{} - menu update {}, button {} of {}", __FUNCTION__, m_overlayCount,
              m_buttonDrawn, input->GetTotalButtons());
  }
}

void CDSDvdNavigator::ClearMenu()
{
  m_menu.reset();
  m_buttonDrawn = -1;
  m_spu.Reset();

  std::unique_lock<CCriticalSection> overlayLock(m_overlayLock);
  if (!m_overlay)
    return;

  CLog::Log(LOGDEBUG, "{} - taking the disc's menu off the screen", __FUNCTION__);
  m_overlay.reset();
  m_overlayPending = true;
}

bool CDSDvdNavigator::TakeOverlay(std::shared_ptr<CDVDOverlayGroup>& overlay)
{
  std::unique_lock<CCriticalSection> lock(m_overlayLock);

  if (!m_overlayPending)
    return false;

  overlay = m_overlay;
  m_overlayPending = false;
  return true;
}

void CDSDvdNavigator::MenuPlaneSize(int& width, int& height) const
{
  // What the disc composes its subpicture against is its own picture, which is 720 wide on
  // every DVD ever made and either 480 or 576 high
  width = 720;
  height = 480;

  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  if (!input)
    return;

  const VideoStreamInfo info = input->GetVideoStreamInfo();
  if (info.width > 0 && info.height > 0)
  {
    width = info.width;
    height = info.height;
  }
}

void CDSDvdNavigator::EndStill()
{
  // Harmless when the disc is not holding anything: still_skip only acts on a still
  if (std::shared_ptr<CDVDInputStreamNavigator> input = Input())
    input->SkipStill();

  m_still = false;
  m_stillIsIndefinite = false;
}

int CDSDvdNavigator::Title() const
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();

  // The input stream keeps this up to date from DVDNAV_CELL_CHANGE, so nothing is asked of
  // libdvdnav here and this is safe to call while a read is in progress
  return input ? input->GetTitle() : 0;
}

int CDSDvdNavigator::Chapter() const
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  return input ? input->GetChapter() : 0;
}

int CDSDvdNavigator::TitleDuration() const
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  return input ? input->GetTotalTime() : 0;
}

void CDSDvdNavigator::Abort()
{
  if (std::shared_ptr<CDVDInputStreamNavigator> input = Input())
  {
    CLog::Log(LOGWARNING, "{} - giving up on the disc", __FUNCTION__);
    input->Abort();
  }
}

void CDSDvdNavigator::AnnounceProgrammeChanges()
{
  m_titleSeen = Title();
  m_announceChanges = true;
}

void CDSDvdNavigator::NoteTitle()
{
  const int title = Title();
  if (title == m_titleSeen)
    return;

  // Exchanged rather than assigned: the reading thread and whichever thread is carrying a key
  // press both come through here, and only one of them should announce a move
  const int previous = m_titleSeen.exchange(title);
  if (previous == title)
    return;

  // Nothing to announce while the disc is still finding its way in and no graph has been
  // built on any of it yet
  if (previous < 0 || !m_announceChanges)
    return;

  CLog::Log(LOGINFO, "{} - the disc moved from title {} to {}", __FUNCTION__, previous, title);

  // A splitter parses its stream once, so it cannot follow the disc across this
  CDSPlayer::NoteDiscProgrammeChanged();
}

bool CDSDvdNavigator::ShowMenu()
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  if (!input)
    return false;

  // The still is left alone, for the same reason as in OnSelect: asking for the menu is a
  // jump command, and letting the still go first only moves the machine on before it arrives
  std::unique_lock<CCriticalSection> lock(m_discLock);
  const bool shown = input->OnMenu();

  m_still = false;
  m_stillIsIndefinite = false;
  CDSDvdStream::LetTheDiscRunOn();
  CLog::Log(LOGDEBUG, "{} - asking the disc for its menu: {}", __FUNCTION__,
            shown ? "accepted" : "refused");
  return shown;
}

void CDSDvdNavigator::OnBack()
{
  if (std::shared_ptr<CDVDInputStreamNavigator> input = Input())
  {
    std::unique_lock<CCriticalSection> lock(m_discLock);
    input->OnBack();
  }
}

void CDSDvdNavigator::MoveSelection(const char* what, void (CDVDInputStreamNavigator::*move)())
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  if (!input)
    return;

  int before = 0;
  int after = 0;
  int buttons = 0;
  {
    std::unique_lock<CCriticalSection> lock(m_discLock);
    before = input->GetCurrentButton();
    (input.get()->*move)();
    after = input->GetCurrentButton();
    buttons = input->GetTotalButtons();
  }

  // The disc's own selected button, before and after. "The disc will not answer its menu"
  // has been several different faults on the Blu-ray side and telling them apart always
  // started here: a button that moves means the key reached the disc and it acted, and a
  // menu that then looks wrong is a drawing problem rather than an input one. Zero buttons
  // means the NAV packet libdvdnav is sitting on carries no menu at all, which is a third
  // thing again.
  CLog::Log(LOGDEBUG, "{} - {}: button {} -> {}, of {} on this menu", __FUNCTION__, what, before,
            after, buttons);

  if (after != before)
    PublishMenu();
}

void CDSDvdNavigator::OnUp()
{
  MoveSelection("up", &CDVDInputStreamNavigator::OnUp);
}

void CDSDvdNavigator::OnDown()
{
  MoveSelection("down", &CDVDInputStreamNavigator::OnDown);
}

void CDSDvdNavigator::OnLeft()
{
  MoveSelection("left", &CDVDInputStreamNavigator::OnLeft);
}

void CDSDvdNavigator::OnRight()
{
  MoveSelection("right", &CDVDInputStreamNavigator::OnRight);
}

void CDSDvdNavigator::OnSelect()
{
  std::shared_ptr<CDVDInputStreamNavigator> input = Input();
  if (!input)
    return;

  // The still is deliberately left alone. A menu waiting on an indefinite still is waiting
  // for exactly this, and libdvdnav comes out of it by itself when the button's command
  // moves the machine somewhere new. Skipping the still first instead -- which is what this
  // did at first -- advances the machine past the menu before the button is pressed, so the
  // activation lands on a disc that has already moved on and nothing whatever happens: the
  // selection moved correctly, the disc reported six buttons, and choosing one did nothing.
  const bool wasStill = m_still;

  int button = 0;
  int buttons = 0;
  int title = 0;
  {
    std::unique_lock<CCriticalSection> lock(m_discLock);
    button = input->GetCurrentButton();
    buttons = input->GetTotalButtons();
    title = input->GetTitle();
    input->ActivateButton();
  }

  // The machine has been told to move, so stop refusing to read on its behalf, and read it
  // flat out for a moment: a disc acts on a button only as it is read, and at the pace a menu
  // plays at that took fifteen seconds to reach the feature
  m_still = false;
  m_stillIsIndefinite = false;
  CDSDvdStream::LetTheDiscRunOn();

  CLog::Log(LOGDEBUG, "{} - activating button {} of {} on title {}{}", __FUNCTION__, button,
            buttons, title, wasStill ? ", which was holding a still" : "");

  // Choosing is what makes the disc change programme, and it happens on this thread
  NoteTitle();
}

int CDSDvdNavigator::OnDiscNavResult(void* pData, int iMessage)
{
  // Any event may be the one that follows a change of programme
  NoteTitle();

  // Called from inside CDVDInputStreamNavigator::ProcessBlock, on the thread that is reading
  // the disc. What is returned here decides what that read does next, so unlike the Blu-ray
  // case these values matter.
  switch (iMessage)
  {
    case DVDNAV_STILL_FRAME:
    {
      const auto* still = static_cast<dvdnav_still_event_t*>(pData);
      const uint8_t seconds = still ? still->length : 0;

      if (!m_still)
      {
        m_still = true;
        m_stillIsIndefinite = (seconds == STILL_FOREVER);
        m_stillUntil = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);

        CLog::Log(LOGDEBUG, "{} - holding the picture {}", __FUNCTION__,
                  m_stillIsIndefinite ? "until the viewer chooses"
                                      : std::to_string(seconds) + "s");
      }

      // Hold rather than skip. Skipping walks straight past whatever the still is holding
      // the picture for, which on a DVD is most often the menu itself.
      return NAVRESULT_HOLD;
    }

    case DVDNAV_WAIT:
      // libdvdnav is asking for our decoding pipeline to drain before it goes on, because it
      // is further ahead in the stream than what is on screen. There is no way to ask that of
      // a DirectShow graph from here, and answering NOP makes ProcessBlock skip the wait for
      // us. The cost is a possible glitch at the join; the alternative is a stream that stops.
      return NAVRESULT_NOP;

    case DVDNAV_VTS_CHANGE:
      // A different video title set: genuinely a different programme, possibly at a different
      // resolution and aspect. NoteTitle above reports it and the graph is rebuilt; here the
      // hold is simply released so the disc keeps moving meanwhile.
      CLog::Log(LOGDEBUG, "{} - the disc moved to another video title set", __FUNCTION__);
      return NAVRESULT_NOP;

    case DVDNAV_CELL_CHANGE:
      // Title and chapter numbers have moved, which NoteTitle has already looked at. The
      // count is what the source filter cuts a presented stream on, see Cell().
      ++m_cellsSeen;
      return NAVRESULT_NOP;

    case DVDNAV_SPU_CLUT_CHANGE:
    {
      // The sixteen colours the disc's menus are drawn from, as YCrCb packed into uint32s.
      // Without these a menu decodes to a picture with no colours at all.
      const uint8_t* clut = static_cast<const uint8_t*>(pData);
      if (clut)
      {
        for (int i = 0; i < 16; i++)
        {
          const uint8_t* entry = clut + i * 4;
          m_spu.m_clut[i][0] = entry[2]; // Y
          m_spu.m_clut[i][1] = entry[0]; // Cr
          m_spu.m_clut[i][2] = entry[1]; // Cb
        }
        m_spu.m_bHasClut = true;
      }
      return NAVRESULT_NOP;
    }

    case DVDNAV_SPU_STREAM_CHANGE:
      // A different subpicture stream is wanted, so whatever was half decoded belongs to the
      // old one
      m_spu.FlushCurrentPacket();
      return NAVRESULT_NOP;

    case DVDNAV_HIGHLIGHT:
      // The selection has moved. The picture is unchanged; only the colours over the chosen
      // button are, so it is drawn again with the new ones.
      if (m_menu)
        PublishMenu();
      return NAVRESULT_NOP;

    case DVDNAV_AUDIO_STREAM_CHANGE:
    case DVDNAV_NAV_PACKET:
    case DVDNAV_HOP_CHANNEL:
    case DVDNAV_NOP:
      return NAVRESULT_NOP;

    case DVDNAV_STOP:
      CLog::Log(LOGINFO, "{} - the disc has finished", __FUNCTION__);
      m_finished = true;
      return NAVRESULT_ERROR;

    case DVDNAV_ERROR:
      CLog::Log(LOGERROR, "{} - libdvdnav gave up on the disc", __FUNCTION__);
      m_finished = true;
      return NAVRESULT_ERROR;

    default:
      CLog::Log(LOGDEBUG, "{} - unhandled navigation result {}", __FUNCTION__, iMessage);
      return NAVRESULT_NOP;
  }
}

void CDSDvdNavigator::GetVideoResolution(unsigned int& width, unsigned int& height)
{
  // The size menu overlays are composed for. It has to match what they are finally drawn
  // onto, not the disc's own 720x480.
  const RESOLUTION_INFO res = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo();
  width = static_cast<unsigned int>(res.iWidth);
  height = static_cast<unsigned int>(res.iHeight);
}

#endif
