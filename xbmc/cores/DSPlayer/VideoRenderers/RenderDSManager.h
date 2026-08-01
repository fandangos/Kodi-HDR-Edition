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
