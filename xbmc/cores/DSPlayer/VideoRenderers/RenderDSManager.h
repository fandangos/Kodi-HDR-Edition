#pragma once

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

#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif

#include <atomic>
#include <list>

#include "windowing/Resolution.h"
#include "threads/CriticalSection.h"
#include "cores/VideoSettings.h"
#include "Videorenderers/WinDsRenderer.h"
#include "VideoRenderers/MPCVRRenderer.h"
#include "../VideoPlayer/Videorenderers/baserenderer.h"
#include "../VideoPlayer/Videorenderers/DebugRenderer.h"
#include "../VideoPlayer/Videorenderers/OverlayRenderer.h"
#include "threads/Event.h"
#include "threads/systemclock.h"

class IPaintCallback;
class CWinDSRenderer;
class CMPCVRRenderer;
class CRenderDSManager;

class IRenderDSMsg
{
  friend CRenderDSManager;
protected:
  virtual void SetRenderOnDS(bool bRender) = 0;
  virtual void SetDSWndVisible(bool bVisible) = 0;
  virtual void VideoParamsChange() = 0;
  virtual void GetDebugInfo(std::string &audio, std::string &video, std::string &general) = 0;
};

class CRenderDSManager
{
public:
  CRenderDSManager(IRenderDSMsg* port);
  ~CRenderDSManager();

  // Functions called from render thread
  void GetVideoRect(CRect &source, CRect &dest, CRect &view) const;
  float GetAspectRatio() const;
  void Update();
  void FrameMove();
  bool HasFrame();
  void Render(bool clear, DWORD flags = 0, DWORD alpha = 255, bool gui = true);
  bool IsGuiLayer();
  bool IsVideoLayer();
  RESOLUTION GetResolution();
  void UpdateResolution();
  void TriggerUpdateResolution(float fps, int width, int flags);
  void SetViewMode(int iViewMode);
  void PreInit(DIRECTSHOW_RENDERER renderer);
  void UnInit();

  /*!
   * rief Stop drawing into the video renderer, and wait until nothing is still doing it
   *
   * Taking the graph down destroys the video renderer. madVR only finishes going away once
   * the application thread has stopped drawing into the surfaces it shares with it, so the
   * thread doing the closing has to know that drawing has actually stopped, not merely been
   * asked to.
   */
  void StopRenderingIntoDirectShow();

  /*!
   * \brief Join the handshake above for one piece of drawing
   *
   * Returns false when the graph is going away, and then the caller must not touch the
   * renderer's shared surfaces at all. While it returns true the caller is counted as
   * drawing, and StopRenderingIntoDirectShow() waits for that count to reach zero. Anything
   * on the application thread that reaches into the video renderer needs this, not just
   * Render(): a Blu-ray rebuilding its graph closes from another thread while this one is
   * still rendering frames.
   *
   * Prefer the scoped CDrawingIntoDirectShow below to calling these directly.
   */
  bool EnterRenderingIntoDirectShow();
  void LeaveRenderingIntoDirectShow();

  //! Scoped form of Enter/LeaveRenderingIntoDirectShow. Test it: if (!drawing) return;
  class CDrawingIntoDirectShow
  {
  public:
    explicit CDrawingIntoDirectShow(CRenderDSManager& manager)
      : m_manager(manager), m_entered(manager.EnterRenderingIntoDirectShow())
    {
    }
    ~CDrawingIntoDirectShow()
    {
      if (m_entered)
        m_manager.LeaveRenderingIntoDirectShow();
    }
    CDrawingIntoDirectShow(const CDrawingIntoDirectShow&) = delete;
    CDrawingIntoDirectShow& operator=(const CDrawingIntoDirectShow&) = delete;
    explicit operator bool() const { return m_entered; }

  private:
    CRenderDSManager& m_manager;
    const bool m_entered;
  };

  bool Flush();
  bool IsConfigured() const;
  void ToggleDebug();
  void Reset();

  // Functions called from GUI
  bool Supports(ERENDERFEATURE feature) const;
  bool Supports(ESCALINGMETHOD method) const;

  double GetDisplayLatency() { return m_displayLatency; }

  bool Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags);
  void DisplayChange(bool bExternalChange);
  void EndRender();
  
  void SetVideoSettings(const CVideoSettings& settings);

  
protected:

  void PresentSingle(bool clear, DWORD flags, DWORD alpha);

  /*!
   * \brief Draw the menus of a Blu-ray that is running its own navigation
   *
   * Called with the render target already pointing at the layer that is composited over
   * the video, so whatever is drawn here lands on top of the picture the renderer shows.
   */
  void RenderBlurayMenu();

  bool Configure();
  void CreateRenderer();
  void DeleteRenderer();

  CDebugRenderer m_debugRenderer;
  //! Draws the disc's own menu overlays. Kodi's overlay renderer already knows how to turn
  //! the images libbluray produces into something on screen, for both HDMV and BD-J discs.
  OVERLAY::CRenderer m_blurayMenuRenderer;
  //! The size of the plane the disc composed its menu against, which is the disc's own video
  //! size and not necessarily anything else on screen: an HDMV disc places its buttons at
  //! pixel positions in this space, and they have to be scaled out of it. Taken from the
  //! overlay itself, because only the disc knows it.
  //! \{
  int m_blurayMenuWidth{0};
  int m_blurayMenuHeight{0};
  //! \}
  //! Counts drawn menu frames, only so the first few can be traced
  uint64_t m_blurayMenuFrames{0};
  //! Counts calls to Render, so the log shows whether Kodi's render loop is still turning
  uint64_t m_renderCalls{0};
  //! Counts per-frame calls on the application thread, so the log shows whether it is alive
  uint64_t m_frameMoves{0};
  //! Set while the graph is going away, to keep drawing off the renderer being destroyed
  std::atomic<bool> m_closingDown{false};
  //! How many threads are inside Render right now, so a close can wait for them to leave
  std::atomic<int> m_rendering{0};
  std::shared_ptr<CBaseRenderer> m_pRenderer;
  mutable CCriticalSection m_statelock;
  CCriticalSection m_datalock;
  bool m_bTriggerUpdateResolution;
  bool m_bTriggerDisplayChange;
  
  bool m_renderDebug;
  XbmcThreads::EndTime<> m_debugTimer;
  enum EPRESENTSTEP
  {
    PRESENT_IDLE     = 0
  , PRESENT_READY
  };

  enum ERENDERSTATE
  {
    STATE_UNCONFIGURED = 0,
    STATE_CONFIGURING,
    STATE_CONFIGURED,
  };
  ERENDERSTATE m_renderState;
  CEvent m_stateEvent;
  bool m_bWaitingForRenderOnDS;
  RESOLUTION m_Resolution;

  double m_displayLatency;
  void UpdateDisplayLatency();
  unsigned int m_width, m_height, m_dwidth, m_dheight;
  unsigned int m_flags;
  float m_fps;
  bool m_bPreInit;
  std::string m_stereomode;

  CEvent m_flushEvent;
  IRenderDSMsg *m_playerPort;
  DIRECTSHOW_RENDERER  m_currentRenderer;
};
