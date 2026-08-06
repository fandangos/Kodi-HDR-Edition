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
#pragma once

#include "threads/Event.h"
#include "IDSPlayer.h"
#include "d3d9.h"
#include "D3d11.h"
#include "guilib/d3dresource.h"
#include <wrl/client.h>
#include "threads/CriticalSection.h"
enum SHAREDRENDER_STATE
{
  RENDERFRAME_LOCK,
  RENDERFRAME_UNLOCK
};

class CRenderWait
{
public:
  CRenderWait();
  void Wait(int ms);
  void Lock();
  void Unlock();
  XbmcThreads::ConditionVariable m_presentevent;
  CCriticalSection m_presentlock;
  SHAREDRENDER_STATE m_renderState;
};

class CBaseSharedRender : public IDSRendererPaintCallback
{

public:
  CBaseSharedRender();
  virtual ~CBaseSharedRender();
  HRESULT CreateTextures(ID3D11Device* pD3DDeviceKodi, IDirect3DDevice9Ex* pD3DDeviceDS, int width, int height);
  HRESULT CreateD3D11Textures(int width, int height);

  // IDSRendererPaintCallback
  virtual void BeginRender() {};
  virtual void RenderToTexture(DS_RENDER_LAYER layer) {};
  virtual void EndRender() {};
  virtual void IncRenderCount();

protected:
  void ResetRenderCount();
  bool GuiVisible(DS_RENDER_LAYER layer = RENDER_LAYER_ALL);

  HRESULT CreateSharedResource(IDirect3DTexture9** ppTexture9, ID3D11Texture2D** ppTexture11);
  HRESULT CreateFakeStaging(ID3D11Texture2D** ppTexture);
  HRESULT ForceComplete();
  
  HRESULT RenderInternal(DS_RENDER_LAYER layer);  
  HRESULT RenderTexture(DS_RENDER_LAYER layer);
  HRESULT SetupVertex();
  HRESULT StoreDeviceState();
  HRESULT SetupDeviceState();
  HRESULT ReStoreDeviceState();

  ID3D11Device*             m_pD3DDeviceKodi = nullptr;
  IDirect3DDevice9Ex*       m_pD3DDeviceDS = nullptr;

  IDirect3DVertexBuffer9*   m_pDSVertexBuffer = nullptr;
  IDirect3DTexture9*        m_pDSUnderTexture = nullptr;
  IDirect3DTexture9*        m_pDSOverTexture = nullptr;

  ID3D11Texture2D*          m_pKodiUnderTexture = nullptr;
  ID3D11Texture2D*          m_pKodiOverTexture = nullptr;
  ID3D11Texture2D*          m_pKodiFakeStaging = nullptr;
  
  CD3DTexture               m_pMPCUnderTexture;
  CD3DTexture               m_pMPCOverTexture;
  CD3DTexture               m_pMPCFakeStaging;

  DWORD m_dwWidth = 0;
  DWORD m_dwHeight = 0;
  float m_fColor[4];
  bool m_bGuiVisible = false;
  bool m_bGuiVisibleOver = false;
  bool m_bWaitKodiRendering;
  int m_renderUnderCount = 0;
  int m_renderOverCount = 0;
  DS_RENDER_LAYER m_currentVideoLayer = RENDER_LAYER_UNDER;

  CRenderWait m_kodiWait;
  CRenderWait m_dsWait;
  // stored DSPlayer Renderer device state
  IDirect3DVertexShader9* m_pOldVS = nullptr;
  IDirect3DVertexBuffer9* m_pOldStreamData = nullptr;
  IDirect3DBaseTexture9* m_pOldTexture = nullptr;

  DWORD m_dwOldFVF = 0;
  UINT  m_nOldOffsetInBytes = 0;
  UINT  m_nOldStride = 0;
  RECT  m_oldScissorRect;

  DWORD m_D3DRS_CULLMODE = 0;
  DWORD m_D3DRS_LIGHTING = 0;
  DWORD m_D3DRS_ZENABLE = 0;
  DWORD m_D3DRS_ALPHABLENDENABLE = 0;
  DWORD m_D3DRS_SRCBLEND = 0;
  DWORD m_D3DRS_DESTBLEND = 0;

  IDirect3DPixelShader9* m_pPix = nullptr;

};
