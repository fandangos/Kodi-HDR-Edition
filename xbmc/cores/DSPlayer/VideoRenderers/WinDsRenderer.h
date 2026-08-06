#pragma once

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

#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif

#if !defined(_LINUX) && !defined(HAS_GL) && defined(HAS_DS_PLAYER)

#include "threads/CriticalSection.h"
#include "guilib/D3DResource.h"
#include "cores/VideoSettings.h"
#include "../VideoPlayer/VideoRenderers/Videoshaders/ShaderFormats.h"
#include "../VideoPlayer/VideoRenderers/BaseRenderer.h"
#include "../VideoPlayer/VideoRenderers/windows/RendererBase.h"
#include "../VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"

#define AUTOSOURCE -1

class CBaseTexture;

class CWinDsRenderer : public CBaseRenderer
{
public:
  CWinDsRenderer();
  ~CWinDsRenderer();

  static CBaseRenderer* Create(CVideoBuffer* buffer);
  static bool Register();


  virtual void         Update();
  virtual void         SetupScreenshot();
  void                 CreateThumbnail(CBaseTexture *texture, unsigned int width, unsigned int height){};

  // Player functions
  bool Configure(const VideoPicture& picture, float fps, unsigned int orientation) override;
  void AddVideoPicture(const VideoPicture& picture, int index) override;
  void RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha) override;
  bool Supports(ESCALINGMETHOD method) const override;
  bool Supports(ERENDERFEATURE feature) const override;
  bool ConfigChanged(const VideoPicture& picture) override;

  virtual bool         Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags, AVPixelFormat format, unsigned extended_format, unsigned int orientation);
#if TODO
  virtual int          GetImage(YV12Image *image, int source = AUTOSOURCE, bool readonly = false) { return 0; };
#endif
  virtual void         ReleaseImage(int source, bool preserve = false) {};
  virtual unsigned int DrawSlice(unsigned char *src[], int stride[], int w, int h, int x, int y) { return 0; };
  virtual void         FlipPage(int source) {};
  virtual void         PreInit();
  virtual void         UnInit();
  virtual void         Reset(); /* resets renderer after seek for example */
  virtual bool         IsConfigured() { return m_bConfigured; }
  virtual void         Flush();

  // Feature support
  virtual bool         SupportsMultiPassRendering() { return false; }

  void                 RenderUpdate(bool clear, unsigned int flags = 0, unsigned int alpha = 255);

protected:
  virtual void         Render(DWORD flags);

  bool                 m_bConfigured;
  DWORD                m_clearColour;
  unsigned int         m_flags;
  CRect                m_oldVideoRect;
 
};
#endif