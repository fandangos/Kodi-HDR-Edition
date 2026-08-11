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
  // So by default we promote the GUI surface to BT2020-PQ and enable the per-shader PQ encode,
  // compositing the overlay in HDR space with no SDR tonemap. Self-gates: SetHDR only succeeds
  // when the HDR display setting is on and the EGL BT2020-PQ/ST2086 extensions are present,
  // otherwise it returns false and we transparently fall back to the sRGB path.
  // advancedsettings <video><androidhdrguisurface>false</> forces the old sRGB path back, for a
  // device that advertises the extensions but composites them wrongly.
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
      // Give the GUI surface the SAME HDR mastering / content-light metadata the video plane
      // signals to the display. Matching exactly means adding the GUI layer to composition
      // doesn't change the HDMI HDR envelope, so the Shield doesn't re-negotiate (and re-sync)
      // the display every time the GUI/overlay redraws. VideoPicture is non-copyable, so copy
      // just the fields SetHDR reads into a stand-in.
      //
      // This is done HERE, synchronously, and not deferred to a later frame. Deferring it was
      // tried (to stop the display being driven into plain HDR10 before the Dolby Vision plane
      // came up, which costs an extra HDMI handshake at playback start) and made colour
      // WORSE: a clip change drops the HDMI link for a few seconds, and a deferred promotion
      // lands the EGL surface rebuild in the middle of that renegotiation, so the display
      // latches its mode from an intermediate state and Dolby Vision does not re-engage.
      // Rebuilding the surface before the link drops is what keeps the negotiation clean.
      VideoPicture hdrPicture;
      // Always ask for BT2020. SetHDR derives the surface colorspace from color_space and only
      // takes its PQ branch for BT2020/BT709; anything else (an m2ts clip that carries no VUI
      // colour description reaches us as AVCOL_SPC_UNSPECIFIED, which is common on Blu-ray menu
      // clips and on the Dolby Vision base layer) made it silently leave the surface sRGB - and,
      // because it then compared EGL_NONE against EGL_NONE, still report success. The GUI surface
      // is ours, not a passthrough of the video's matrix: an HDR picture always wants BT2020-PQ.
      hdrPicture.color_space = AVCOL_SPC_BT2020_NCL;
      hdrPicture.hasDisplayMetadata = picture.hasDisplayMetadata;
      hdrPicture.displayMetadata = picture.displayMetadata;
      hdrPicture.hasLightMetadata = picture.hasLightMetadata;
      hdrPicture.lightMetadata = picture.lightMetadata;
      pqGuiSurface = CServiceBroker::GetWinSystem()->SetHDR(&hdrPicture);
      CLog::Log(LOGINFO,
                "CRendererMediaCodecSurface::Configure: HDR GUI surface {} (source "
                "colorspace {}, transfer {}, hdrType {}, metadata: {})",
                pqGuiSurface ? "enabled (BT2020-PQ)" : "requested but unavailable, using sRGB",
                static_cast<int>(picture.color_space), static_cast<int>(picture.color_transfer),
                static_cast<int>(picture.hdrType),
                picture.hasDisplayMetadata ? "matched to video" : "none in stream");
    }
    else
    {
      // An SDR clip after an HDR one: the surface is no longer torn down between clips
      // (see Reset), so it has to be handed back explicitly or SDR GUI content would be
      // drawn into a leftover PQ surface.
      CServiceBroker::GetWinSystem()->SetHDR(nullptr);
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
  //
  // Skipped when this renderer is only being replaced by one for the next clip: a Blu-ray
  // playlist tears the renderer down and rebuilds it at every m2ts boundary, and reverting
  // here meant the GUI EGL surface was destroyed and recreated TWICE per transition
  // (PQ -> sRGB -> PQ), each rebuild re-registering the only HDR layer SurfaceFlinger sees,
  // in the same few milliseconds the video decoder is being re-instantiated. The next
  // Configure() re-asserts the correct colorspace either way, and runs under the render
  // manager's locks with no frame in between, so leaving it alone across a clip change costs
  // nothing and keeps that state still.
  if (!m_transientRelease)
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
