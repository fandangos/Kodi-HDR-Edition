/*
 *  Copyright (C) 2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MPCVRRenderer.h"


#include "ServiceBroker.h"
#include "rendering/dx/DirectXHelper.h"
#include "rendering/dx/RenderContext.h"
#include "utils/MemUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "d3d11_1.h"
#include <Filters/MPCVideoRenderer/DSResource.h>
#include "guilib/GUIShaderDX.h"
#include "StreamsManager.h"
#include "filesystem/Directory.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"

#define GetDevice DX::DeviceResources::Get()->GetD3DDevice()

using namespace Microsoft::WRL;



std::shared_ptr<CMPCVRRenderer> CMPCVRRenderer::Get()
{
  static std::shared_ptr<CMPCVRRenderer> sVideoRenderer(new CMPCVRRenderer);
  return sVideoRenderer;
}

CMPCVRRenderer::CMPCVRRenderer()
{
  m_pPlacebo = nullptr;
  Init();
}

CMPCVRRenderer::~CMPCVRRenderer()
{
  m_pPlacebo->Release();
  Release();
}

void CMPCVRRenderer::Init()
{
  CD3DDSPixelShader shdr;
  LPVOID data;
  DWORD size;
  D3D11_INPUT_ELEMENT_DESC Layout[] = {
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
  };

  if (!shdr.LoadFromFile(IDF_VS_11_SIMPLE))
    CLog::Log(LOGERROR, "{} failed loading {}", __FUNCTION__, IDF_VS_11_SIMPLE);

  data = shdr.GetData();
  size = shdr.GetSize();
  GetDevice->CreateVertexShader(data, size, nullptr, &m_pVS_Simple);
  GetDevice->CreateInputLayout(Layout, std::size(Layout), data, size, &m_pVSimpleInputLayout);



  if (!shdr.LoadFromFile(IDF_PS_11_SIMPLE))
    CLog::Log(LOGERROR, "{} failed loading {}", __FUNCTION__, IDF_VS_11_SIMPLE);

  data = shdr.GetData();
  size = shdr.GetSize();

  GetDevice->CreatePixelShader(data, size, nullptr, &m_pPS_Simple);

  if (!shdr.LoadFromFile(IDF_PS_11_SIMPLE))
    CLog::Log(LOGERROR, "{} failed loading {}", __FUNCTION__, IDF_PS_11_SIMPLE);
  data = shdr.GetData();
  size = shdr.GetSize();
  GetDevice->CreatePixelShader(data, size, nullptr, &m_pPS_BitmapToFrame);
  if (!m_pPlacebo)
    m_pPlacebo = new PL::CPlHelper();
}

void CMPCVRRenderer::Release()
{
  
  m_IntermediateTarget.Release();
  
  m_pVS_Simple = nullptr;
  m_pVertexBuffer = nullptr;
  m_pVSimpleInputLayout = nullptr;
  m_pPS_Simple = nullptr;
  m_pPlacebo->Release();

}

HRESULT CMPCVRRenderer::FillVertexBuffer(const UINT srcW, const UINT srcH, const CRect& srcRect, const int iRotation, const bool bFlip)
{
  DS_VERTEX Vertices[4];
  FillVertices(Vertices, srcW, srcH, srcRect, iRotation, bFlip);

  D3D11_MAPPED_SUBRESOURCE mr;
  HRESULT hr = DX::DeviceResources::Get()->GetD3DContext()->Map(m_pVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mr);
  if (FAILED(hr)) {
    CLog::LogF(LOGINFO, "FillVertexBuffer() : Map() failed");
    return hr;
  }

  memcpy(mr.pData, &Vertices, sizeof(Vertices));
  DX::DeviceResources::Get()->GetD3DContext()->Unmap(m_pVertexBuffer.Get(), 0);

  return hr;
}

void CMPCVRRenderer::FillVertices(DS_VERTEX(&Vertices)[4], const UINT srcW, const UINT srcH, const CRect& srcRect, const int iRotation, const bool bFlip)
{
  const float src_dx = 1.0f / srcW;
  const float src_dy = 1.0f / srcH;
  float src_l = src_dx * srcRect.x1;
  float src_r = src_dx * srcRect.x2;
  const float src_t = src_dy * srcRect.y1;
  const float src_b = src_dy * srcRect.y2;

  POINT points[4];
  switch (iRotation) {
  case 90:
    points[0] = { -1, +1 };
    points[1] = { +1, +1 };
    points[2] = { -1, -1 };
    points[3] = { +1, -1 };
    break;
  case 180:
    points[0] = { +1, +1 };
    points[1] = { +1, -1 };
    points[2] = { -1, +1 };
    points[3] = { -1, -1 };
    break;
  case 270:
    points[0] = { +1, -1 };
    points[1] = { -1, -1 };
    points[2] = { +1, +1 };
    points[3] = { -1, +1 };
    break;
  default:
    points[0] = { -1, -1 };
    points[1] = { -1, +1 };
    points[2] = { +1, -1 };
    points[3] = { +1, +1 };
  }

  if (bFlip) {
    std::swap(src_l, src_r);
  }

  // Vertices for drawing whole texture
  // 2 ___4
  //  |\ |
  // 1|_\|3
  Vertices[0] = { {(float)points[0].x, (float)points[0].y, 0}, {src_l, src_b} };
  Vertices[1] = { {(float)points[1].x, (float)points[1].y, 0}, {src_l, src_t} };
  Vertices[2] = { {(float)points[2].x, (float)points[2].y, 0}, {src_r, src_b} };
  Vertices[3] = { {(float)points[3].x, (float)points[3].y, 0}, {src_r, src_t} };
}

HRESULT CMPCVRRenderer::CreateVertexBuffer()
{
  if (m_pVertexBuffer.Get())
    return S_OK;
  DS_VERTEX Vertices[4];

  CD3D11_BUFFER_DESC bufferDesc(sizeof(Vertices), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);

  HRESULT hr = DX::DeviceResources::Get()->GetD3DDevice()->CreateBuffer(&bufferDesc, NULL, m_pVertexBuffer.ReleaseAndGetAddressOf());

  return hr;
}

void CMPCVRRenderer::CopyToBackBuffer()
{
  if (!m_pVS_Simple.Get())
    Init();

  ID3D11DeviceContext* pContext = DX::DeviceResources::Get()->GetD3DContext();
  const UINT Stride = sizeof(DS_VERTEX);
  const UINT Offset = 0;

  D3D11_VIEWPORT VP;
  VP.TopLeftX = (FLOAT)m_destRect.x1;
  VP.TopLeftY = (FLOAT)m_destRect.y1;  
  VP.Width = (FLOAT)m_destRect.Width();
  VP.Height = (FLOAT)m_destRect.Height();
  VP.MinDepth = 0.0f;
  VP.MaxDepth = 1.0f;
  CRect sourcerect;
  sourcerect.x1 = 0;
  sourcerect.y1 = 0;
  sourcerect.x2 = m_sourceWidth;
  sourcerect.y2 = m_sourceHeight;
  FillVertexBuffer(m_destRect.Width(), m_destRect.Height(), m_destRect/*m_sourceRect*/, 0, 0);
  // Set resources
  pContext->OMSetRenderTargets(1, DX::DeviceResources::Get()->GetBackBuffer().GetAddressOfRTV(), nullptr);
  pContext->RSSetViewports(1, &VP);
  //Need blend state?
  //pContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
  pContext->VSSetShader(m_pVS_Simple.Get(), nullptr, 0);
  pContext->PSSetShader(m_pPS_Simple.Get(), nullptr, 0);

  pContext->IASetInputLayout(m_pVSimpleInputLayout.Get());

  pContext->PSSetShaderResources(0, 1, m_IntermediateTarget.GetAddressOfSRV());
  pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  pContext->IASetVertexBuffers(0, 1, m_pVertexBuffer.GetAddressOf(), &Stride, &Offset);

  // Draw textured quad onto render target
  pContext->Draw(4, 0);
}

void CMPCVRRenderer::DrawSubtitles()
{
  if (CStreamsManager::Get()->SubtitleManager)
  {
    
    ID3D11DeviceContext1* pContext = DX::DeviceResources::Get()->GetD3DContext();
    const auto rtStart = m_rSubTime;

    // Set render target and shaders
    pContext->IASetInputLayout(m_pVSimpleInputLayout.Get());
    pContext->VSSetShader(m_pVS_Simple.Get(), nullptr, 0);
    pContext->PSSetShader(m_pPS_BitmapToFrame.Get(), nullptr, 0);
    Com::SmartRect m_windowRect(Com::SmartPoint(0, 0), Com::SmartPoint(GetScreenRect().x2, GetScreenRect().y2));
    Com::SmartRect pSrc, pDst;
    //sending the devicecontext to the subtitlemanager he will draw directly with it
    CStreamsManager::Get()->SubtitleManager->AlphaBlt(pContext, pSrc, pDst, m_windowRect);

  }
}

void CMPCVRRenderer::DrawStats()
{
  //no text no draw
  if (m_statsText.length() == 0)
    return;

  SIZE rtSize{ (LONG) m_screenRect.Width(),(LONG) m_screenRect.Height()};

  //m_StatsBackground.Draw(DX::DeviceResources::Get()->GetBackBuffer().GetRenderTarget(), rtSize);

  //TODO Add alpha and color config in gui or advanced settings for osd
  m_Font3D.Draw2DText(DX::DeviceResources::Get()->GetBackBuffer().GetRenderTarget(), rtSize, m_StatsTextPoint.x, m_StatsTextPoint.y, COLOR_ARGB(55, 255, 255, 255), m_statsText.c_str());
  static int col = m_StatsRect.right;
  if (--col < m_StatsRect.left) {
    col = m_StatsRect.right;
  }
}

void CMPCVRRenderer::Reset()
{
  m_IntermediateTarget.Release();
}

void CMPCVRRenderer::Render(int index,
                           int index2,
                           CD3DTexture& target,
                           const CRect& sourceRect,
                           const CRect& destRect,
                           const CRect& viewRect,
                           unsigned flags)
{
  m_iBufferIndex = index;

  CreateIntermediateTarget(m_viewWidth, m_viewHeight, false);

  Render(target, sourceRect, destRect, viewRect, flags);
}

void CMPCVRRenderer::Render(CD3DTexture& target, const CRect& sourceRect, const CRect& destRect, const CRect& viewRect, unsigned flags)
{
  if (m_iNumBuffers == 0)
    return;
  if (m_viewWidth != static_cast<unsigned>(viewRect.Width()) ||
    m_viewHeight != static_cast<unsigned>(viewRect.Height()))
  {
    m_viewWidth = static_cast<unsigned>(viewRect.Width());
    m_viewHeight = static_cast<unsigned>(viewRect.Height());
    
  }
  // Restore our view port.
  DX::Windowing()->RestoreViewPort();
  DX::Windowing()->ApplyStateBlock();
}

bool CMPCVRRenderer::Configure(const VideoPicture& picture, float fps, unsigned int orientation)
{
  m_bConfigured = true;

  return true;
}

bool CMPCVRRenderer::Flush(bool saveBuffers)
{
  CLog::Log(LOGDEBUG, "{}", __FUNCTION__);
  return false;
}

void CMPCVRRenderer::RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha)
{

  if (clear)
    CServiceBroker::GetWinSystem()->GetGfxContext().Clear(DX::Windowing()->UseLimitedColor() ? 0x101010 : 0);
  DX::Windowing()->SetAlphaBlendEnable(alpha < 255);
  
  ManageRenderArea();
  //m_sourceRect source of the video
  //m_destRect destination rectangle
  //GetScreenRect screen rectangle
  //destRect destination
  //m_sourceWidth
  //m_sourceHeight
  D3D11_VIEWPORT oldVP;
  UINT oldIVP = 1;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRT;
  ID3D11DeviceContext* pContext = DX::DeviceResources::Get()->GetD3DContext();
  pContext->OMGetRenderTargets(1, &oldRT, nullptr);
  pContext->RSGetViewports(&oldIVP, &oldVP);

  CopyToBackBuffer();

  DrawSubtitles();

  DrawStats();

  CRenderSystemDX* renderingDX = dynamic_cast<CRenderSystemDX*>(CServiceBroker::GetRenderSystem());
  renderingDX->GetGUIShader()->ApplyStateBlock();

  ID3D11ShaderResourceView* views[4] = {};
  pContext->PSSetShaderResources(0, 4, views);

  pContext->OMSetRenderTargets(1, oldRT.GetAddressOf(), nullptr);
  pContext->RSSetViewports(1, &oldVP);

  DX::Windowing()->SetAlphaBlendEnable(true);
}

bool CMPCVRRenderer::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps)
{
  if (m_sourceWidth != width
    || m_sourceHeight != height)
  {
    m_sourceWidth = width;
    m_sourceHeight = height;
    // need to recreate textures
  }
  CreateVertexBuffer();
  m_sourceRect.x1 = 0;
  m_sourceRect.x2 = width;
  m_sourceRect.y1 = 0;
  m_sourceRect.y2 = height;
  m_fps = fps;
  CalculateFrameAspectRatio(width, height);
  SetViewMode(m_videoSettings.m_ViewMode);
  //CreateInputTarget(m_sourceWidth, m_sourceHeight);
  CreateIntermediateTarget(width, height, false);

  SetGraphSize();

  return true;
}

void CMPCVRRenderer::SetGraphSize()
{
  m_screenRect = CRect(0,0, GetScreenRect().x2, GetScreenRect().y2);

  HRESULT hr3 = m_Font3D.InitDeviceObjects();
  
  if (SUCCEEDED(hr3)) {
    hr3 = m_StatsBackground.InitDeviceObjects();
    hr3 = m_Rect3D.InitDeviceObjects();
    hr3 = m_Underlay.InitDeviceObjects();
    hr3 = m_Lines.InitDeviceObjects();
    hr3 = m_SyncLine.InitDeviceObjects();
   
  }

  if (!m_screenRect.IsEmpty()) {
    SIZE rtSize{ (LONG)m_screenRect.Width(),(LONG)m_screenRect.Height()};

    if (m_iResizeStats == 0) {
      int w = std::max((float)512, m_screenRect.Width() / 2 - 10) - 5 - 3;
      int h = std::max((float)280, m_screenRect.Height() - 10) - 5 - 3;
      m_StatsFontH = (int)std::ceil(std::min(w / 36.0, h / 19.4));
      m_StatsFontH &= ~1;
      if (m_StatsFontH < 14) {
        m_StatsFontH = 14;
      }
    }
    else {
      m_StatsFontH = 14;
    }
    //Consolas
    if (S_OK == m_Font3D.CreateFontBitmap(L"Space Mono", m_StatsFontH, 0)) {
      SIZE charSize = m_Font3D.GetMaxCharMetric();
      m_StatsRect.right = m_StatsRect.left + 61 * charSize.cx + 5 + 3;
      m_StatsRect.bottom = m_StatsRect.top + 18 * charSize.cy + 5 + 3;
    }
    
    m_StatsBackground.Set(m_StatsRect, rtSize, D3DCOLOR_ARGB(CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("dsplayer.vr.osdalpha"), 0, 0, 0));

    m_Yaxis = m_GraphRect.bottom - 50 * m_Yscale;

    m_Underlay.Set(m_GraphRect, rtSize, D3DCOLOR_ARGB(CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("dsplayer.vr.osdalpha"), 0, 0, 0));

    m_Lines.ClearPoints(rtSize);
    POINT points[2];
    const int linestep = 20 * m_Yscale;
    for (int y = m_GraphRect.top + (m_Yaxis - m_GraphRect.top) % (linestep); y < m_GraphRect.bottom; y += linestep) {
      points[0] = { m_GraphRect.left,  y };
      points[1] = { m_GraphRect.right, y };
      m_Lines.AddPoints(points, std::size(points), (y == m_Yaxis) ? D3DCOLOR_XRGB(150, 150, 255) : D3DCOLOR_XRGB(100, 100, 255));
    }
    m_Lines.UpdateVertexBuffer();
  }
}

CD3DTexture& CMPCVRRenderer::GetIntermediateTarget()
{
  if (m_IntermediateTarget.Get() == nullptr)
  {
  }
  return m_IntermediateTarget;
}

CRect CMPCVRRenderer::GetScreenRect() const
{
  CRect screenRect(0.f, 0.f,
    static_cast<float>(CServiceBroker::GetWinSystem()->GetGfxContext().GetWidth()),
    static_cast<float>(CServiceBroker::GetWinSystem()->GetGfxContext().GetHeight()));

  switch (CServiceBroker::GetWinSystem()->GetGfxContext().GetStereoMode())
  {
  case RenderStereoMode::SPLIT_HORIZONTAL:
    screenRect.y2 *= 2;
    break;
  case RenderStereoMode::SPLIT_VERTICAL:
    screenRect.x2 *= 2;
    break;
  default:
    break;
  }

  return screenRect;
}

void CMPCVRRenderer::GetVideoRect(CRect& source, CRect& dest, CRect& view) const
{
  source = m_sourceRect;
  dest = m_destRect;
  view = m_viewRect;
}

float CMPCVRRenderer::GetAspectRatio() const
{
  CLog::Log(LOGDEBUG, "{}", __FUNCTION__);
  return 0.0f;
}

void CMPCVRRenderer::SettingOptionsRenderMethodsFiller(const std::shared_ptr<const CSetting>& setting, std::vector<IntegerSettingOption>& list, int& current, void* data)
{
  CLog::Log(LOGDEBUG, "{}", __FUNCTION__);
}

void CMPCVRRenderer::SetVideoSettings(const CVideoSettings& settings)
{
  CLog::Log(LOGDEBUG, "{}", __FUNCTION__);
}



bool CMPCVRRenderer::CreateIntermediateTarget(unsigned width,
                                             unsigned height,
                                             bool dynamic,
                                             DXGI_FORMAT format)
{
  
  // No format specified by renderer
  if (format == DXGI_FORMAT_UNKNOWN)
    format = DX::Windowing()->GetBackBuffer().GetFormat();

  // don't create new one if it exists with requested size and format
  if (m_IntermediateTarget.Get() && m_IntermediateTarget.GetFormat() == format &&
      m_IntermediateTarget.GetWidth() == width && m_IntermediateTarget.GetHeight() == height)
    return true;

  if (m_IntermediateTarget.Get())
    m_IntermediateTarget.Release();

  CLog::LogF(LOGDEBUG, "{} creating intermediate target {}x{} format {}.", __FUNCTION__, width, height,
             DX::DXGIFormatToString(format));

  if (!m_IntermediateTarget.Create(width,height, 1,
                                   dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT, format,nullptr,0U,"CMPCVRRenderer Intermediate Target"))
  {
    CLog::LogF(LOGERROR, "intermediate target creation failed.");
    return false;
  } 
  return true;
}

void CMPCVRRenderer::CheckVideoParameters()
{
}


