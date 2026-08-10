/*
 *  Copyright (C) 2007-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RendererMediaCodecSurface.h"

#include "../RenderFactory.h"
#include "../RenderFlags.h"
#include "DVDCodecs/Video/DVDVideoCodecAndroidMediaCodec.h"
#include "ServiceBroker.h"
#include "rendering/RenderSystem.h"
#include "settings/AdvancedSettings.h"
#include "settings/MediaSettings.h"
#include "settings/SettingsComponent.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "windowing/WinSystem.h"

#include "platform/android/activity/XBMCApp.h"

#include <chrono>
#include <thread>

CRendererMediaCodecSurface::CRendererMediaCodecSurface()
{
  CLog::Log(LOGINFO, "Instancing CRendererMediaCodecSurface");
}

CRendererMediaCodecSurface::~CRendererMediaCodecSurface()
{
  Reset();
}

CBaseRenderer* CRendererMediaCodecSurface::Create(CVideoBuffer *buffer)
{
  if (buffer && dynamic_cast<CMediaCodecVideoBuffer*>(buffer) && !dynamic_cast<CMediaCodecVideoBuffer*>(buffer)->HasSurfaceTexture())
    return new CRendererMediaCodecSurface();
  return nullptr;
}

bool CRendererMediaCodecSurface::Register()
{
  VIDEOPLAYER::CRendererFactory::RegisterRenderer("mediacodec_surface", CRendererMediaCodecSurface::Create);
  return true;
}

bool CRendererMediaCodecSurface::Configure(const VideoPicture &picture, float fps, unsigned int orientation)
{
  CLog::Log(LOGINFO, "CRendererMediaCodecSurface::Configure");

  m_sourceWidth = picture.iWidth;
  m_sourceHeight = picture.iHeight;
  m_renderOrientation = orientation;

  m_iFlags = GetFlagsChromaPosition(picture.chroma_position) |
             GetFlagsColorMatrix(picture.color_space, picture.iWidth, picture.iHeight) |
             GetFlagsColorPrimaries(picture.color_primaries) |
             GetFlagsStereoMode(picture.stereoMode);

  // Calculate the input frame aspect ratio.
  CalculateFrameAspectRatio(picture.iDisplayWidth, picture.iDisplayHeight);
  SetViewMode(m_videoSettings.m_ViewMode);

  // Overlay-over-HDR compositing.
  //
  // The per-shader PQ path (GraphicContext::SetTransferPQ -> KODI_TRANSFER_PQ,
  // "rgb *= m_sdrPeak") only produces correct output when the GUI EGL surface is itself
  // created as EGL_GL_COLORSPACE_BT2020_PQ (CWinSystemAndroidGLESContext::SetHDR). On the
  // MediaCodec surface path the video is a separate Android surface, so by default SetHDR
  // is never called and the GUI surface stays sRGB: SurfaceFlinger then tonemaps the SDR
  // overlay onto the HDR output, which desaturates BD-J/HDMV disc-menu overlays.
  //
  // Default behaviour (known good): keep the GUI in sRGB and let SurfaceFlinger composite.
  // Experimental opt-in (advancedsettings <video><androidhdrguisurface>true): promote the
  // GUI surface to BT2020-PQ and enable the per-shader PQ encode so the overlay is
  // composited in HDR space with no SDR tonemap. Self-gates: SetHDR only succeeds when the
  // HDR display setting is on and the EGL BT2020-PQ/ST2086 extensions are present, otherwise
  // it returns false and we transparently fall back to the sRGB path.
  bool pqGuiSurface = false;
  const auto advancedSettings = CServiceBroker::GetSettingsComponent()->GetAdvancedSettings();
  if (advancedSettings && advancedSettings->m_videoAndroidHDRGuiSurface &&
      CServiceBroker::GetWinSystem()->IsHDRDisplaySettingEnabled())
  {
    const bool pictureIsHdr = picture.color_transfer == AVCOL_TRC_SMPTE2084 ||
                              picture.color_transfer == AVCOL_TRC_ARIB_STD_B67 ||
                              picture.hdrType == StreamHdrType::HDR_TYPE_DOLBYVISION;
    if (pictureIsHdr)
    {
      // Make the GUI surface BT2020-PQ and give it the SAME HDR mastering / content-light
      // metadata the video plane signals to the display. Matching exactly means adding the GUI
      // layer to composition doesn't change the HDMI HDR envelope, so the Shield doesn't
      // re-negotiate (and re-sync) the display every time the GUI/overlay redraws. VideoPicture
      // is non-copyable, so copy just the fields SetHDR reads into a stand-in.
      VideoPicture hdrPicture;
      hdrPicture.color_space = picture.color_space;
      hdrPicture.hasDisplayMetadata = picture.hasDisplayMetadata;
      hdrPicture.displayMetadata = picture.displayMetadata;
      hdrPicture.hasLightMetadata = picture.hasLightMetadata;
      hdrPicture.lightMetadata = picture.lightMetadata;
      pqGuiSurface = CServiceBroker::GetWinSystem()->SetHDR(&hdrPicture);
      CLog::Log(LOGINFO,
                "CRendererMediaCodecSurface::Configure: experimental HDR GUI surface {} (metadata: {})",
                pqGuiSurface ? "enabled (BT2020-PQ)" : "requested but unavailable, using sRGB",
                picture.hasDisplayMetadata ? "matched to video" : "none in stream");
    }
  }

  CServiceBroker::GetWinSystem()->GetGfxContext().SetTransferPQ(pqGuiSurface);

  return true;
}

CRenderInfo CRendererMediaCodecSurface::GetRenderInfo()
{
  CRenderInfo info;
  info.max_buffer_size = 4;
  return info;
}

void CRendererMediaCodecSurface::AddVideoPicture(const VideoPicture &picture, int index)
{
  ReleaseBuffer(index);

  BUFFER &buf(m_buffers[index]);
  if (picture.videoBuffer)
  {
    buf.videoBuffer = picture.videoBuffer;
    buf.videoBuffer->Acquire();
  }
}

void CRendererMediaCodecSurface::ReleaseVideoBuffer(int idx, bool render)
{
  BUFFER &buf(m_buffers[idx]);
  if (buf.videoBuffer)
  {
    CMediaCodecVideoBuffer *mcvb(dynamic_cast<CMediaCodecVideoBuffer*>(buf.videoBuffer));
    if (mcvb)
    {
      if (render && m_bConfigured)
        mcvb->RenderUpdate(m_surfDestRect, CXBMCApp::Get().GetNextFrameTime());
      else
        mcvb->ReleaseOutputBuffer(render, 0);
    }
    buf.videoBuffer->Release();
    buf.videoBuffer = nullptr;
  }
}

void CRendererMediaCodecSurface::ReleaseBuffer(int idx)
{
  ReleaseVideoBuffer(idx, false);
}

bool CRendererMediaCodecSurface::Supports(ERENDERFEATURE feature) const
{
  if (feature == RENDERFEATURE_ZOOM || feature == RENDERFEATURE_STRETCH ||
      feature == RENDERFEATURE_PIXEL_RATIO || feature == RENDERFEATURE_VERTICAL_SHIFT ||
      feature == RENDERFEATURE_ROTATION)
    return true;

  return false;
}

void CRendererMediaCodecSurface::Reset()
{
  for (int i = 0 ; i < 4 ; ++i)
    ReleaseVideoBuffer(i, false);
  m_lastIndex = -1;

  CServiceBroker::GetWinSystem()->GetGfxContext().SetTransferPQ(false);
  // Revert an experimental BT2020-PQ GUI surface back to sRGB. No-op when it was never
  // promoted (SetHDR only recreates the surface if the colorspace actually changes).
  CServiceBroker::GetWinSystem()->SetHDR(nullptr);
}

void CRendererMediaCodecSurface::RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha)
{
  m_bConfigured = true;

  // this hack is needed to get the 2D mode of a 3D movie going
  RenderStereoMode stereo_mode = CServiceBroker::GetWinSystem()->GetGfxContext().GetStereoMode();
  if (stereo_mode != RenderStereoMode::OFF)
    CServiceBroker::GetWinSystem()->GetGfxContext().SetStereoView(RenderStereoView::LEFT);

  ManageRenderArea();

  if (stereo_mode != RenderStereoMode::OFF)
    CServiceBroker::GetWinSystem()->GetGfxContext().SetStereoView(RenderStereoView::OFF);

  m_surfDestRect = m_destRect;
  switch (stereo_mode)
  {
    case RenderStereoMode::SPLIT_HORIZONTAL:
      m_surfDestRect.y2 *= 2.0;
      break;
    case RenderStereoMode::SPLIT_VERTICAL:
      m_surfDestRect.x2 *= 2.0;
      break;
    case RenderStereoMode::MONO:
      if (CONF_FLAGS_STEREO_MODE_MASK(m_iFlags) == CONF_FLAGS_STEREO_MODE_TAB)
        m_surfDestRect.y2 = m_surfDestRect.y2 * 2.0f;
      else
        m_surfDestRect.x2 = m_surfDestRect.x2 * 2.0f;
      break;
    default:
      break;
  }

  if (index != m_lastIndex)
  {
    ReleaseVideoBuffer(index, true);
    m_lastIndex = index;
  }
}

void CRendererMediaCodecSurface::ReorderDrawPoints()
{
  CBaseRenderer::ReorderDrawPoints();

  // Handle orientation
  switch (m_renderOrientation)
  {
    case 90:
    case 270:
    {
      double scale = static_cast<double>(m_surfDestRect.Height() / m_surfDestRect.Width());
      int diff = static_cast<int>(static_cast<double>(m_surfDestRect.Height()) * scale -
                                  static_cast<double>(m_surfDestRect.Width())) /
                 2;
      m_surfDestRect = CRect(m_surfDestRect.x1 - diff, m_surfDestRect.y1, m_surfDestRect.x2 + diff, m_surfDestRect.y2);
    }
    default:
      break;
  }
}
