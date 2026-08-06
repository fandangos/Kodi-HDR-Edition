/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
 
#if HAS_DS_PLAYER

#include "WinDsRenderer.h"
#include "Util.h"
#include "settings/Settings.h"
#include "guilib/Texture.h"
//#include "windowing/windows/WinSystemWin32DX.h"
#include "settings/AdvancedSettings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "FileSystem/File.h"
#include "utils/MathUtils.h"
#include "StreamsManager.h"
#include "settings/DisplaySettings.h"
#include "settings/MediaSettings.h"
#include "windowing/GraphicContext.h"
#include "application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "cores/dsplayer/IDSPlayer.h"
#include "windowing/windows/WinSystemWin32DX.h"
#include "rendering/dx/rendercontext.h"
#include "cores/videoplayer/videorenderers/RenderFactory.h"
#include "ServiceBroker.h"
#include "threads/CriticalSection.h"

CWinDsRenderer::CWinDsRenderer(): 
  m_bConfigured(false)
  , m_oldVideoRect(0, 0, 0, 0)
{
  PreInit();
}

CWinDsRenderer::~CWinDsRenderer()
{
  UnInit();
}

CBaseRenderer* CWinDsRenderer::Create(CVideoBuffer* buffer)
{
  return new CWinDsRenderer();
}

bool CWinDsRenderer::Register()
{
  VIDEOPLAYER::CRendererFactory::RegisterRenderer("default", Create);
  return true;
}

void CWinDsRenderer::SetupScreenshot()
{
}

bool CWinDsRenderer::Configure(const VideoPicture& picture, float fps, unsigned int orientation)
{
  m_bConfigured = true;

  return true;
}

void CWinDsRenderer::AddVideoPicture(const VideoPicture& picture, int index)
{
}

void CWinDsRenderer::RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha)
{
  CRenderSystemDX* renderSystem = dynamic_cast<CRenderSystemDX*>(CServiceBroker::GetRenderSystem());
  if (clear)
    CServiceBroker::GetWinSystem()->GetGfxContext().Clear(m_clearColour);

  if (alpha < 255)
    renderSystem->SetAlphaBlendEnable(true);
  else
    renderSystem->SetAlphaBlendEnable(false);

  if (!m_bConfigured)
    return;
  
  //do we need lock CSingleLock lock(g_graphicsContext);
  //std::unique_lock<CCriticalSection>
  ManageRenderArea();

  Render(flags);
}


bool CWinDsRenderer::ConfigChanged(const VideoPicture& picture)
{
  return false;
}

bool CWinDsRenderer::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags, AVPixelFormat format, unsigned extended_format, unsigned int orientation)
{
  if (m_sourceWidth != width
    || m_sourceHeight != height)
  {
    m_sourceWidth = width;
    m_sourceHeight = height;
    // need to recreate textures
  }

  m_fps = fps;
  m_iFlags = flags;
  m_flags = flags;
  m_format = format;

  // calculate the input frame aspect ratio
  CalculateFrameAspectRatio(d_width, d_height);

  SetViewMode(m_videoSettings.m_ViewMode);
  ManageRenderArea();

  m_bConfigured = true;

  return true;
}

void CWinDsRenderer::Reset()
{
  // todo
}

void CWinDsRenderer::Update()
{
  if (!m_bConfigured) 
    return;

  ManageRenderArea();
}


void CWinDsRenderer::RenderUpdate(bool clear, unsigned int flags, unsigned int alpha)
{
  if (clear)
    CServiceBroker::GetWinSystem()->GetGfxContext().Clear(DX::Windowing()->UseLimitedColor() ? 0x101010 : 0);

  DX::Windowing()->SetAlphaBlendEnable(alpha < 255);

  if (!m_bConfigured)
    return;

  ManageRenderArea();

  Render(flags);
  DX::Windowing()->SetAlphaBlendEnable(true);
}

void CWinDsRenderer::Flush()
{
  PreInit();
  SetViewMode(CMediaSettings::GetInstance().GetDefaultVideoSettings().m_ViewMode);
  ManageRenderArea();

  m_bConfigured = true;
}

void CWinDsRenderer::PreInit()
{
  CSingleExit lock(CServiceBroker::GetWinSystem()->GetGfxContext());
  m_bConfigured = false;
  UnInit();

  // setup the background colour
  m_clearColour = DX::Windowing()->UseLimitedColor() ? (16 * 0x010101) : 0;
  return;
}

void CWinDsRenderer::UnInit()
{
  m_bConfigured = false;
}

void CWinDsRenderer::Render(DWORD flags)
{
  // TODO: Take flags into account
  /*if( flags & RENDER_FLAG_NOOSD ) 
    return;*/

  //CSingleExit lock(CServiceBroker::GetWinSystem()->GetGfxContext());
  
  if (m_oldVideoRect != m_destRect)
  {
    CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>()->SetPosition(m_sourceRect, m_destRect, m_viewRect);
    m_oldVideoRect = m_destRect;
  }

  
}

bool CWinDsRenderer::Supports(ESCALINGMETHOD method) const
{
  if(method == VS_SCALINGMETHOD_NEAREST
  || method == VS_SCALINGMETHOD_LINEAR)
    return true;

  return false;
}

bool CWinDsRenderer::Supports( ERENDERFEATURE method ) const
{
  if ( method == RENDERFEATURE_CONTRAST
    || method == RENDERFEATURE_BRIGHTNESS
	|| method == RENDERFEATURE_ZOOM
	|| method == RENDERFEATURE_VERTICAL_SHIFT
	|| method == RENDERFEATURE_PIXEL_RATIO
	|| method == RENDERFEATURE_POSTPROCESS)
    return true;

  return false;
}

#endif
