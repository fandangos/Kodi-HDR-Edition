/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#if HAS_DS_PLAYER

#include "RenderDSManager.h"
#include "cores/VideoPlayer/Videorenderers/RenderFlags.h"
#include "threads/SingleLock.h"
#include "threads/SystemClock.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "utils/StringUtils.h"

#include "application/Application.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "windowing/GraphicContext.h"
#include "cores/DataCacheCore.h"
#include "GraphFilters.h"

#include "DSBlurayNavigator.h"
#include "DSDvdNavigator.h"
#include "DSGraph.h"
#include "StreamsManager.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlay.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlayImage.h"
#include "cores/VideoPlayer/DVDCodecs/Overlay/DVDOverlaySpu.h"
#include "guilib/GUITexture.h"

#include "utils/CPUInfo.h"
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "rendering/RenderSystem.h"
#include "windowing/windows/WinSystemWin32DX.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include <chrono>
#include <cmath>

using namespace KODI::MESSAGING;
using namespace std::chrono_literals;

CRenderDSManager::CRenderDSManager(IRenderDSMsg* port) :
  m_pRenderer(nullptr),
  m_bTriggerUpdateResolution(false),
  m_bTriggerDisplayChange(false),
  m_renderDebug(false),
  m_bWaitingForRenderOnDS(true),
  m_bPreInit(false),
  m_Resolution(RES_INVALID),
  m_renderState(STATE_UNCONFIGURED),
  m_displayLatency(0.0),
  m_width(0),
  m_height(0),
  m_dwidth(0),
  m_dheight(0),
  m_fps(0.0f),
  m_playerPort(port)
{
}

CRenderDSManager::~CRenderDSManager()
{
  m_pRenderer.reset();
}

void CRenderDSManager::GetVideoRect(CRect &source, CRect &dest, CRect &view) const
{
  std::unique_lock<CCriticalSection> lock(m_statelock);
  if (m_pRenderer)
    m_pRenderer->GetVideoRect(source, dest, view);
}

float CRenderDSManager::GetAspectRatio() const
{
  CSingleExit lock(m_statelock);
  if (m_pRenderer)
    return m_pRenderer->GetAspectRatio();
  else
    return 1.0f;
}

bool CRenderDSManager::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags)
{
  // check if something has changed
  {
    CSingleExit lock(m_statelock);

    // The frame rate is worked out from the time between frames, so it wanders by a
    // fraction. Comparing it exactly made every frame look like a change of format: each
    // one put the renderer back into configuring, which made it report that it was not
    // rendering, which brought it straight back here. On a Blu-ray menu, looping every few
    // seconds, that loop eventually wedged the player.
    // Nothing having changed only means there is nothing to do if the renderer is actually
    // configured. After a graph is rebuilt on the same disc every one of these still holds
    // the last graph's values, so this said "no change" to a renderer that had been torn
    // down and never configured it: the picture played, drawn by the video renderer itself,
    // but Kodi drew nothing over it - no OSD, and no mouse pointer.
    if (m_width == width &&
      m_height == height &&
      m_dwidth == d_width &&
      m_dheight == d_height &&
      std::fabs(m_fps - fps) < 0.001f &&
      (m_flags & ~CONF_FLAGS_FULLSCREEN) == (flags & ~CONF_FLAGS_FULLSCREEN) &&
      m_pRenderer != NULL &&
      m_renderState == STATE_CONFIGURED)
      return true;
  }

  CLog::Log(LOGDEBUG, "CRenderDSManager::Configure - change configuration. %dx%d. display: %dx%d. framerate: %4.2f.", width, height, d_width,d_height, fps);
  {
    CSingleExit lock(m_statelock);
    m_width = width;
    m_height = height;
    m_dwidth = d_width;
    m_dheight = d_height;
    m_fps = fps;
    m_flags = flags;
    m_renderState = STATE_CONFIGURING;
    m_stateEvent.Reset();
    m_bWaitingForRenderOnDS = true;

  }

  if (!m_stateEvent.Wait(1000ms))
  {
    CLog::Log(LOGWARNING, "CRenderDSManager::Configure - timeout waiting for configure");
    return false;
  }

  CSingleExit lock(m_statelock);
  if (m_renderState != STATE_CONFIGURED)
  {
    CLog::Log(LOGWARNING, "CRenderDSManager::Configure - failed to configure");
    return false;
  }

  return true;
}

bool CRenderDSManager::Configure()
{

  // lock all interfaces
  std::unique_lock<CCriticalSection> lock(m_statelock);
  std::unique_lock<CCriticalSection> lock2(m_datalock);

  if (!m_pRenderer)
  {
    CreateRenderer();
    if (!m_pRenderer)
      return false;
  }
  bool result;
  if (m_currentRenderer == DIRECTSHOW_RENDERER_MADVR)
  {
    result = reinterpret_cast<CWinDsRenderer*>(m_pRenderer.get())->Configure(m_width, m_height, m_dwidth, m_dheight, m_fps, m_flags, (AVPixelFormat)0, 0, 0);
    
  }
  else if (m_currentRenderer == DIRECTSHOW_RENDERER_MPCVR)
  {
    result = CMPCVRRenderer::Get()->Configure(m_width, m_height, m_dwidth, m_dheight, m_fps);
    m_pRenderer = CMPCVRRenderer::Get();
  }

   
  if (result)
  {
    CRenderInfo info = m_pRenderer->GetRenderInfo();
    int renderbuffers = info.max_buffer_size;

    m_pRenderer->Update();
    m_bTriggerUpdateResolution = true;
    m_renderState = STATE_CONFIGURED;
  }
  else
    m_renderState = STATE_UNCONFIGURED;

  m_stateEvent.Set();
  m_playerPort->VideoParamsChange();
  return result;
}

void CRenderDSManager::Reset()
{

  if (m_pRenderer && m_currentRenderer == DIRECTSHOW_RENDERER_MADVR)
    reinterpret_cast<CWinDsRenderer*>(m_pRenderer.get())->Reset();

}

bool CRenderDSManager::IsConfigured() const
{
  CSingleExit lock(m_statelock);
  if (m_renderState == STATE_CONFIGURED)
    return true;
  else
    return false;
}

void CRenderDSManager::Update()
{
  if (m_pRenderer)
    m_pRenderer->Update();
}


bool CRenderDSManager::HasFrame()
{
  if (!IsConfigured())
    return false;

    return true;
}

void CRenderDSManager::FrameMove()
{
  // Runs on the application thread every frame. If this stops, that thread is stuck
  // somewhere else; if it continues while Render never runs, the video window was never
  // brought to the front.
  if ((m_frameMoves++ % 200) == 0)
    CLog::Log(LOGDEBUG, "{} - frame move {}, state {}, waiting for DS {}", __FUNCTION__,
              m_frameMoves, static_cast<int>(m_renderState), m_bWaitingForRenderOnDS);

  UpdateResolution();

  {
    std::unique_lock<CCriticalSection> lock(m_statelock);

    if (m_renderState == STATE_UNCONFIGURED)
      return;
    else if (m_renderState == STATE_CONFIGURING)
    {
      lock.unlock();
      if (!Configure())
        return;

      if (m_flags & CONF_FLAGS_FULLSCREEN)
      {
        CServiceBroker::GetAppMessenger()->PostMsg(TMSG_SWITCHTOFULLSCREEN);
      }
    }
    if (m_renderState == STATE_CONFIGURED && m_bWaitingForRenderOnDS )//&& CServiceBroker::GetWinSystem()->GetGfxContext().IsFullScreenVideo()) removed for mpcvr
    {
      m_bWaitingForRenderOnDS = false;
      m_bPreInit = false;
      m_playerPort->SetRenderOnDS(true);
    }
  }
}

void CRenderDSManager::EndRender()
{
  if (m_renderState == STATE_CONFIGURED && !g_application.GetComponent<CApplicationPlayer>()->ReadyDS())
    CServiceBroker::GetWinSystem()->GetGfxContext().Clear(0);
}

void CRenderDSManager::SetVideoSettings(const CVideoSettings& settings)
{
  std::unique_lock<CCriticalSection> lock(m_statelock);
  if (m_pRenderer)
  {
    m_pRenderer->SetVideoSettings(settings);
  }
}


void CRenderDSManager::PreInit(DIRECTSHOW_RENDERER renderer)
{
  
#if TODO
  if (!g_application.IsCurrentThread())
  {
    CLog::Log(LOGERROR, "CRenderDSManager::UnInit - not called from render thread");
    return;
  }
#endif
  CSingleExit lock(m_statelock);

  m_currentRenderer = renderer;
  m_closingDown = false;
  if (!m_pRenderer)
    CreateRenderer();

  UpdateDisplayLatency();

  m_bPreInit = true;
}

void CRenderDSManager::StopRenderingIntoDirectShow()
{
  m_closingDown = true;

  // Wait for whoever is drawing to come out. Bounded: if the drawing thread is stuck for
  // some other reason, waiting forever here only makes a second stuck thread.
  XbmcThreads::EndTime<> timeout{std::chrono::milliseconds(2000)};
  while (m_rendering > 0 && !timeout.IsTimePast())
    KODI::TIME::Sleep(1ms);

  CLog::Log(LOGDEBUG, "{} - drawing into the renderer has stopped ({} still inside)",
            __FUNCTION__, m_rendering.load());
}

bool CRenderDSManager::EnterRenderingIntoDirectShow()
{
  if (m_closingDown)
    return false;

  m_rendering++;

  // The close can land between the test above and the count, and whoever is closing is
  // already waiting on that count - so look again rather than let this one slip through
  // behind their back.
  if (m_closingDown)
  {
    m_rendering--;
    return false;
  }

  return true;
}

void CRenderDSManager::LeaveRenderingIntoDirectShow()
{
  m_rendering--;
}

void CRenderDSManager::UnInit()
{
#if TODO
  if (!g_application.IsCurrentThread())
  {
    CLog::Log(LOGERROR, "CRenderDSManager::UnInit - not called from render thread");
    return;
  }
#endif

  CSingleExit lock(m_statelock);

  m_debugRenderer.Flush();

  DeleteRenderer();

  m_renderState = STATE_UNCONFIGURED;
}

bool CRenderDSManager::Flush()
{
  if (!m_pRenderer)
    return true;
#if TODO
  if (g_application.IsCurrentThread())
  {
    CLog::Log(LOGDEBUG, "{} - flushing renderer", __FUNCTION__);


    CSingleExit exitlock(CServiceBroker::GetWinSystem()->GetGfxContext());

    CSingleExit lock(m_statelock);
    CSingleExit lock3(m_datalock);

    if (m_pRenderer)
    {
      m_pRenderer->Flush(true);
      m_debugRenderer.Flush();
      m_flushEvent.Set();
    }
  }
  else
  {
    m_flushEvent.Reset();
    CServiceBroker::GetAppMessenger()->PostMsg(TMSG_RENDERER_FLUSH);
    if (!m_flushEvent.WaitMSec(1000))
    {
      CLog::Log(LOGERROR, "{} - timed out waiting for renderer to flush", __FUNCTION__);
      return false;
    }
    else
      return true;
  }
#endif
  return true;
}

void CRenderDSManager::CreateRenderer()
{
  m_pRenderer = nullptr;
  if (m_currentRenderer == DIRECTSHOW_RENDERER_MADVR)
    m_pRenderer = std::make_shared<CWinDsRenderer>();
  else if (m_currentRenderer == DIRECTSHOW_RENDERER_MPCVR)
  {
    m_pRenderer= CMPCVRRenderer::Get();
  }
}

void CRenderDSManager::DeleteRenderer()
{
  CLog::Log(LOGDEBUG, "{} - deleting renderer", __FUNCTION__);

  
    
  if (m_currentRenderer == DIRECTSHOW_RENDERER_MPCVR)
    CMPCVRRenderer::Get()->Release();
  m_pRenderer.reset();
}

void CRenderDSManager::SetViewMode(int iViewMode)
{
  CSingleExit lock(m_statelock);
  if (m_pRenderer)
    m_pRenderer->SetViewMode(iViewMode);
  m_playerPort->VideoParamsChange();
}

RESOLUTION CRenderDSManager::GetResolution()
{
  RESOLUTION res = CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution();

  CSingleExit lock(m_statelock);
  if (m_renderState == STATE_UNCONFIGURED)
    return res;
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_ADJUSTREFRESHRATE) != ADJUST_REFRESHRATE_OFF)
    res = CResolutionUtils::ChooseBestResolution(m_fps, m_width, m_height, !m_stereomode.empty());
  return res;
}

void CRenderDSManager::Render(bool clear, DWORD flags, DWORD alpha, bool gui)
{
  // Everything below draws through the video renderer's shared surfaces. Once the graph is
  // being taken down those are being destroyed, and drawing into them keeps the renderer
  // from finishing - which is a deadlock, since the thread destroying it is waiting here.
  CDrawingIntoDirectShow drawing(*this);
  if (!drawing)
    return;

  CSingleExit exitLock(CServiceBroker::GetWinSystem()->GetGfxContext());

  {
    std::unique_lock<CCriticalSection> lock(m_statelock);

    // Heartbeat: whether Kodi's own render loop is still running, and whether it is getting
    // past the state check, decides whether menus can be drawn at all
    if ((m_renderCalls++ % 200) == 0)
      CLog::Log(LOGDEBUG, "{} - render call {}, state {}", __FUNCTION__, m_renderCalls,
                static_cast<int>(m_renderState));

    if (m_renderState != STATE_CONFIGURED)
      return;
  }

  if (m_currentRenderer == DIRECTSHOW_RENDERER_MADVR)
    g_application.GetComponent<CApplicationPlayer>()->RenderToTexture(RENDER_LAYER_OVER);

  // Menus are drawn at the end of this whichever way it goes, so the picture cannot paint
  // over them, and the paths that draw no picture still show them
  if (!gui && m_pRenderer->IsGuiLayer())
  {
    RenderBlurayMenu();
    return;
  }

  if (!gui || m_pRenderer->IsGuiLayer())
  {
      PresentSingle(clear, flags, alpha);
  }

  if (gui)
  {
    if (!m_pRenderer->IsGuiLayer())
      m_pRenderer->Update();
  }

  RenderBlurayMenu();

  //add overlays
#if TODO
    CRect src, dst, view;
    m_pRenderer->GetVideoRect(src, dst, view);

    if (m_renderDebug)
    {
      std::string audio, video, player, cores;

      m_playerPort->GetDebugInfo(audio, video, player);

      cores = StringUtils::Format("W( {} )", g_cpuInfo.GetCoresUsageString().c_str());

      m_debugRenderer.SetInfo(audio, video, player, cores);
      m_debugRenderer.Render(src, dst, view);

      m_debugTimer.Set(1000);
    }
  }
#endif
}

void CRenderDSManager::RenderBlurayMenu()
{
  // TEMPORARY: isolating whether drawing menus from here is what stalls graph building
  static const bool disabled = (getenv("KODI_NO_BLURAY_MENU_DRAW") != nullptr);
  if (disabled)
    return;

  CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get();
  CDSDvdNavigator* dvd = CDSDvdNavigator::Get();

  // Why nothing is being drawn is otherwise invisible: there may be no disc being
  // navigated, or one with nothing to show
  if ((m_renderCalls % 200) == 1)
    CLog::Log(LOGDEBUG, "{} - navigator {}, overlay held {}", __FUNCTION__,
              (navigator || dvd) ? "present" : "MISSING",
              m_blurayMenuRenderer.HasOverlay(0) ? "yes" : "no");

  if (!navigator && !dvd)
    return;

  // A DVD's menu is decoded out of the stream rather than pushed at us on its own plane, but
  // by the time it reaches here it is an overlay like any other and is drawn the same way.
  // What differs is the plane it was composed against: a Blu-ray names it in every overlay,
  // while a DVD's is simply its own picture, 720 across.
  if (dvd)
  {
    int width = 0;
    int height = 0;
    dvd->MenuPlaneSize(width, height);
    if (width > 0 && height > 0)
    {
      m_blurayMenuWidth = width;
      m_blurayMenuHeight = height;
    }
  }

  // The disc replaces its overlay outright rather than amending it, so a new one always
  // supersedes what came before. An empty overlay is how the disc says to clear the menu.
  std::shared_ptr<CDVDOverlayGroup> overlay;
  const bool arrived = navigator ? navigator->TakeOverlay(overlay) : dvd->TakeOverlay(overlay);
  if (arrived)
  {
    const size_t images = overlay ? overlay->m_overlays.size() : 0;
    CLog::Log(LOGDEBUG, "{} - took an overlay of {} image(s)", __FUNCTION__, images);

    // What the disc actually asked to be drawn, and where
    if (overlay)
    {
      for (const auto& image : overlay->m_overlays)
      {
        if (const auto* picture = dynamic_cast<const CDVDOverlayImage*>(image.get()))
        {
          // The plane the disc composes against. Every image of one overlay belongs to the
          // same plane, so the last one seen is as good as the first.
          if (picture->source_width > 0 && picture->source_height > 0)
          {
            m_blurayMenuWidth = picture->source_width;
            m_blurayMenuHeight = picture->source_height;
          }

          CLog::Log(LOGDEBUG,
                    "{} -   image {}x{} at {},{} against a {}x{} source, {} palette entries, "
                    "{} bytes of pixels",
                    __FUNCTION__, picture->width, picture->height, picture->x, picture->y,
                    picture->source_width, picture->source_height, picture->palette.size(),
                    picture->pixels.size());
        }
        else if (const auto* spu = dynamic_cast<const CDVDOverlaySpu*>(image.get()))
        {
          // A DVD menu, which arrives as subpicture rather than as a picture with a palette.
          // The overlay renderer knows how to draw one, highlight colours and all; the plane
          // it belongs to is the disc's own video and was taken from the navigator above.
          CLog::Log(LOGDEBUG, "{} -   a subpicture menu {}x{} at {},{}, highlight {},{} to {},{}",
                    __FUNCTION__, spu->width, spu->height, spu->x, spu->y, spu->crop_i_x_start,
                    spu->crop_i_y_start, spu->crop_i_x_end, spu->crop_i_y_end);
        }
        else
        {
          CLog::Log(LOGDEBUG, "{} -   an overlay that is neither a picture nor a subpicture",
                    __FUNCTION__);
        }
      }
    }

    // Add the pictures, not the group holding them. The overlay renderer converts each
    // overlay it is given into something drawable and has no conversion for a group, so
    // handing it the group drew nothing at all.
    //
    // No overlay at all is how a menu is taken off the screen, so this has to cope with
    // being handed nothing: releasing what was there and adding none is exactly right.
    m_blurayMenuRenderer.Release(0);
    if (overlay)
    {
      for (const auto& image : overlay->m_overlays)
        m_blurayMenuRenderer.AddOverlay(image, 0.0, 0);
    }
  }

  if (!m_blurayMenuRenderer.HasOverlay(0))
    return;

  // Render() releases the graphics lock for its whole length, so that madVR can get on
  // while Kodi waits. Drawing needs it back: this goes to the same immediate context
  // everything else draws with, and that context tolerates exactly one thread at a time.
  std::unique_lock<CCriticalSection> graphicsLock(CServiceBroker::GetWinSystem()->GetGfxContext());

  // Logged for the first few frames only, so a menu that wedges the renderer shows whether
  // it got in and back out again
  const bool trace = (m_blurayMenuFrames < 3);

  // Point drawing back at the layer that is composited over the video. Showing the picture
  // leaves the target somewhere else, and menus drawn there go nowhere anyone can see.
  if (m_currentRenderer == DIRECTSHOW_RENDERER_MADVR)
    g_application.GetComponent<CApplicationPlayer>()->RenderToTexture(RENDER_LAYER_OVER);

  // Three coordinate spaces meet here and none of them can be assumed to match:
  //
  //   - the plane the disc composed its menu against, which is the disc's own idea of its
  //     video size. Measured 1920x1080 on both reference discs, the UHD one included: a
  //     4K disc does not imply a 4K menu plane, and libbluray hands BD-J 1920x1080 anyway.
  //   - madVR's output, which the rectangles below are expressed in,
  //   - Kodi's interface, which is what this draws into.
  //
  // The overlay renderer places an ALIGN_VIDEO overlay at x*(dest/source)+dest.x1, so
  // handing it the same rectangle for source and dest means a scale of exactly 1: the menu
  // is blitted at raw composition pixels. Passing the source rect for both was right while
  // Kodi's interface was itself 1920x1080, which is what it was when this was written; once
  // the interface became 3840x2160 every disc's menu quietly moved into the top left corner
  // at half size. On Saint Seiya that put the selection marker a quarter of the way to the
  // item it was marking, which reads as "the menu is not responding" even though the disc
  // was tracking every key press perfectly.
  CRect madvrSource, madvrDest, madvrView;
  m_pRenderer->GetVideoRect(madvrSource, madvrDest, madvrView);

  const float interfaceWidth =
      static_cast<float>(CServiceBroker::GetWinSystem()->GetGfxContext().GetWidth());
  const float interfaceHeight =
      static_cast<float>(CServiceBroker::GetWinSystem()->GetGfxContext().GetHeight());

  // Where the picture sits, moved out of madVR's output size and into the interface's. They
  // are usually the same size, and when they are not this is the difference that matters.
  const float toInterfaceX =
      madvrView.Width() > 0 ? interfaceWidth / madvrView.Width() : 1.0f;
  const float toInterfaceY =
      madvrView.Height() > 0 ? interfaceHeight / madvrView.Height() : 1.0f;

  CRect dest(madvrDest.x1 * toInterfaceX, madvrDest.y1 * toInterfaceY,
             madvrDest.x2 * toInterfaceX, madvrDest.y2 * toInterfaceY);
  CRect view(0.0f, 0.0f, interfaceWidth, interfaceHeight);

  // A menu that has arrived says what it was composed against; before one has, the video's
  // own size is the best guess and nothing is being drawn anyway.
  CRect source(0.0f, 0.0f,
               m_blurayMenuWidth > 0 ? static_cast<float>(m_blurayMenuWidth)
                                     : madvrSource.Width(),
               m_blurayMenuHeight > 0 ? static_cast<float>(m_blurayMenuHeight)
                                      : madvrSource.Height());

  // A picture madVR has not placed yet leaves nothing to scale into, and drawing the menu
  // into a rectangle of no size is drawing it nowhere
  if (dest.Width() <= 0.0f || dest.Height() <= 0.0f || source.Width() <= 0.0f ||
      source.Height() <= 0.0f)
    return;

  if (trace)
  {
    // Every number that decides where a menu lands, in one line. A menu drawn in one
    // coordinate space and composited in another is the obvious explanation for a menu that
    // is invisible or in the wrong place, and it should be answerable from a log rather than
    // by experiment -- it was not, and that cost an afternoon.
    CLog::Log(LOGDEBUG,
              "{} - drawing menu frame {}: the disc composed against {}x{}, madVR puts the "
              "picture at {},{} {}x{} of {}x{}, the interface is {}x{}, so the menu goes to "
              "{},{} {}x{} scaled by {:.2f}x{:.2f}",
              __FUNCTION__, m_blurayMenuFrames, source.Width(), source.Height(), madvrDest.x1,
              madvrDest.y1, madvrDest.Width(), madvrDest.Height(), madvrView.Width(),
              madvrView.Height(), interfaceWidth, interfaceHeight, dest.x1, dest.y1,
              dest.Width(), dest.Height(), dest.Width() / source.Width(),
              dest.Height() / source.Height());
  }

  // TEMPORARY: a block of colour where the menu buttons are. If this shows and the menu does
  // not, drawing here reaches the screen and the fault is in how the overlays are converted;
  // if neither shows, nothing drawn at this point reaches the screen at all.
  if (getenv("KODI_BLURAY_MENU_PROBE") != nullptr)
    CGUITexture::DrawQuad(CRect(600.0f, 800.0f, 1400.0f, 1000.0f), 0xA0FF0000);

  m_blurayMenuRenderer.SetVideoRect(source, dest, view);
  m_blurayMenuRenderer.Render(0);

  // The shared renderer decides whether to composite this layer by counting what was
  // drawn into it, and the overlay renderer draws without going through CGUITexture
  CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>()->IncRenderCount();

  m_blurayMenuFrames++;
  if (trace)
    CLog::Log(LOGDEBUG, "{} - menu frame drawn", __FUNCTION__);
}

bool CRenderDSManager::IsGuiLayer()
{
  { CSingleExit lock(m_statelock);

    if (!m_pRenderer)
      return false;

    if (m_pRenderer->IsGuiLayer() && HasFrame())
      return true;

    if (m_renderDebug && m_debugTimer.IsTimePast())
      return true;
  }
  return false;
}

bool CRenderDSManager::IsVideoLayer()
{
  { CSingleExit lock(m_statelock);

    if (!m_pRenderer)
      return false;

    if (!m_pRenderer->IsGuiLayer())
      return true;
  }
  return false;
}

/* simple present method */
void CRenderDSManager::PresentSingle(bool clear, DWORD flags, DWORD alpha)
{

  m_pRenderer->RenderUpdate(0,0,clear, flags, alpha);

}

void CRenderDSManager::UpdateDisplayLatency()
{
#if TODO
  float refresh = CServiceBroker::GetWinSystem()->GetGfxContext().GetFPS();
  if (CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution() == RES_WINDOW)
    refresh = 0; // No idea about refresh rate when windowed, just get the default latency
  m_displayLatency = (double) CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->GetDisplayLatency(refresh);

  if (CGraphFilters::Get()->GetAuxAudioDelay())
    m_displayLatency += (double)CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->GetDisplayAuxDelay(refresh);

  g_application.GetComponent<CApplicationPlayer>()->SetAVDelay(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_AudioDelay);

  CLog::Log(LOGDEBUG, "CRenderDSManager::UpdateDisplayLatency - Latency set to %1.0f msec", m_displayLatency * 1000.0f);
#endif
}

void CRenderDSManager::UpdateResolution()
{
#if 1
  if (m_bTriggerDisplayChange)
  {
    if (m_Resolution != RES_INVALID)
    {
      CLog::Log(LOGDEBUG, "{} gui resolution updated by external display change event", __FUNCTION__);
      CServiceBroker::GetWinSystem()->GetGfxContext().SetVideoResolution(m_Resolution,false);
      UpdateDisplayLatency();
    }
    m_bTriggerDisplayChange = false;
    //m_playerPort->VideoParamsChange();
  }
#endif
  if (m_bTriggerUpdateResolution)
  {
    if (CServiceBroker::GetWinSystem()->GetGfxContext().IsFullScreenVideo() && CServiceBroker::GetWinSystem()->GetGfxContext().IsFullScreenRoot())
    {
      if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_ADJUSTREFRESHRATE) != ADJUST_REFRESHRATE_OFF && m_fps > 0.0f)
      {
        //todo add stereo mode here
        RESOLUTION res = CResolutionUtils::ChooseBestResolution(m_fps, m_width, m_height, false);

        CServiceBroker::GetWinSystem()->GetGfxContext().SetVideoResolution(res, false);
        UpdateDisplayLatency(); 
        if (m_pRenderer)
          m_pRenderer->Update();
      }
      m_bTriggerUpdateResolution = false;
    }
   // m_playerPort->VideoParamsChange();
  }

}

void CRenderDSManager::DisplayChange(bool bExternalChange)
{
  // Get Current Display settings
  MONITORINFOEX mi;
  mi.cbSize = sizeof(MONITORINFOEX);
  GetMonitorInfo(MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST), &mi);

  DEVMODE dm;
  ZeroMemory(&dm, sizeof(dm));
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsEx(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm, 0) == FALSE)
    EnumDisplaySettingsEx(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm, 0);

  int width = dm.dmPelsWidth;
  int height = dm.dmPelsHeight;
  bool bInterlaced = (dm.dmDisplayFlags & DM_INTERLACED) ? true : false;
  int iRefreshRate = dm.dmDisplayFrequency;
  float refreshRate;
  if (iRefreshRate == 59 || iRefreshRate == 29 || iRefreshRate == 23)
    refreshRate = static_cast<float>(iRefreshRate + 1) / 1.001f;
  else
    refreshRate = static_cast<float>(iRefreshRate);

  // Convert Current Resolution to Kodi Res
#if TODO
  std::string sRes = StringUtils::Format("%1i%05i%05i%09.5f{}", DX::DeviceResources::Get()->GetCurrentScreen(), width, height, refreshRate, bInterlaced ? "istd" : "pstd");
#else
  std::string sRes = StringUtils::Format("%1i%05i%05i%09.5f{}", 0, width, height, refreshRate, bInterlaced ? "istd" : "pstd");
#endif
  
  RESOLUTION res = CDisplaySettings::GetResolutionFromString(sRes);
  RESOLUTION_INFO res_info = CDisplaySettings::GetInstance().GetResolutionInfo(res);

  if (bExternalChange)
  {
    if (m_bPreInit)
    {
      m_bPreInit = false;
      m_playerPort->SetDSWndVisible(true);
      CLog::Log(LOGDEBUG, "{} showing dsplayer window", __FUNCTION__);
    }

    m_Resolution = res;
    m_bTriggerDisplayChange = true;
    CLog::Log(LOGDEBUG, "{} external display change event update resolution to {}", __FUNCTION__, res_info.strMode.c_str());
  }
  else
  {
    CLog::Log(LOGDEBUG, "{} internal display change event update resolution to {}", __FUNCTION__, res_info.strMode.c_str());
    if (m_bTriggerDisplayChange)
    {  
      m_bTriggerDisplayChange = false;
      CLog::Log(LOGDEBUG, "{} requested gui resolution update by external display change event dropped", __FUNCTION__);
    }
  }
}

void CRenderDSManager::TriggerUpdateResolution(float fps, int width, int flags)
{
  if (width)
  {
    m_fps = fps;
    m_width = width;
    m_flags = flags;
  }
  m_bTriggerUpdateResolution = true;
}

void CRenderDSManager::ToggleDebug()
{
  m_renderDebug = !m_renderDebug;
  m_debugTimer.SetExpired();
}

bool CRenderDSManager::Supports(ERENDERFEATURE feature) const
{
  CSingleExit lock(m_statelock);
  if (m_pRenderer)
    return m_pRenderer->Supports(feature);
  else
    return false;
}

bool CRenderDSManager::Supports(ESCALINGMETHOD method) const
{
  CSingleExit lock(m_statelock);
  if (m_pRenderer)
    return m_pRenderer->Supports(method);
  else
    return false;
}


#endif