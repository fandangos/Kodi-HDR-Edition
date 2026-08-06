/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://xbmc.org
 *
 *      Copyright (C) 2014-2015 Aracnoz
 *      http://github.com/aracnoz/xbmc
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
#include "MadvrSharedRender.h"
#include "windowing/GraphicContext.h"
#include "windowing/windows/WinSystemWin32DX.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"
#include "Application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "mvrInterfaces.h"
#include "settings/SettingsComponent.h"
#include "../../cores/videosettings.h"

#include "settings/AdvancedSettings.h"
#include <DSUtil/Geometry.h>
#include <StreamsManager.h>
#include "ServiceBroker.h"


CMadvrSharedRender::CMadvrSharedRender()
{
  g_application.GetComponent<CApplicationPlayer>()->Register(this);
  m_iOldViewMode = 0;
}

CMadvrSharedRender::~CMadvrSharedRender()
{
  g_application.GetComponent<CApplicationPlayer>()->Unregister(this);
}

HRESULT CMadvrSharedRender::Render(DS_RENDER_LAYER layer)
{

  // Lock madVR thread while kodi rendering
  if (m_bWaitKodiRendering)
    m_dsWait.Wait(100);

  if (!g_application.GetComponent<CApplicationPlayer>()->ReadyDS() || (CServiceBroker::GetWinSystem()->GetGfxContext().IsFullScreenVideo() && layer == RENDER_LAYER_UNDER))
    return CALLBACK_INFO_DISPLAY;

  // Render the GUI on madVR
  RenderInternal(layer);

  // Pull the trigger on the wait for the main Kodi application thread
  if (layer == RENDER_LAYER_OVER)
    m_kodiWait.Unlock();

  // Return to madVR if we rendered something
  if (m_bGuiVisible && !CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_bDisableMadvrLowLatency)
  {
    return CALLBACK_USER_INTERFACE;
  }

  return CALLBACK_INFO_DISPLAY; 
}

void CMadvrSharedRender::BeginRender()
{
  // Wait that madVR complete the rendering
  m_kodiWait.Lock();
  m_kodiWait.Wait(100);

  // Lock madVR thread while kodi rendering
  m_dsWait.Lock();

  // Clear RenderTarget. CreateRenderTargetView leaves the pointer alone when it fails, so it
  // starts null and is only used once it has actually been given something - a texture that
  // has gone away otherwise means clearing and releasing whatever the stack happened to hold.
  ID3D11RenderTargetView* pSurface11 = nullptr;
  ID3D11DeviceContext* pContext = DX::DeviceResources::Get()->GetD3DContext();

  if (SUCCEEDED(m_pD3DDeviceKodi->CreateRenderTargetView(m_pKodiUnderTexture, NULL, &pSurface11)) && pSurface11)
  {
    pContext->ClearRenderTargetView(pSurface11, m_fColor);
    pSurface11->Release();
    pSurface11 = nullptr;
  }

  if (SUCCEEDED(m_pD3DDeviceKodi->CreateRenderTargetView(m_pKodiOverTexture, NULL, &pSurface11)) && pSurface11)
  {
    pContext->ClearRenderTargetView(pSurface11, m_fColor);
    pSurface11->Release();
  }

  // Reset RenderCount
  ResetRenderCount();
}

void CMadvrSharedRender::RenderToTexture(DS_RENDER_LAYER layer)
{
  m_currentVideoLayer = layer;

  ID3D11DeviceContext* pContext = DX::DeviceResources::Get()->GetD3DContext();
  ID3D11RenderTargetView* pSurface11 = nullptr;

  if (SUCCEEDED(m_pD3DDeviceKodi->CreateRenderTargetView(
          layer == RENDER_LAYER_UNDER ? m_pKodiUnderTexture : m_pKodiOverTexture, NULL, &pSurface11)) &&
      pSurface11)
  {
    pContext->OMSetRenderTargets(1, &pSurface11, 0);
    pSurface11->Release();
  }
}

void CMadvrSharedRender::EndRender()
{
  // Force to complete the rendering on Kodi device
  DX::DeviceResources::Get()->FinishCommandList();
  ForceComplete();

  m_bGuiVisible = GuiVisible();
  m_bGuiVisibleOver = GuiVisible(RENDER_LAYER_OVER);

  // Unlock madVR rendering
  m_dsWait.Unlock();
}
