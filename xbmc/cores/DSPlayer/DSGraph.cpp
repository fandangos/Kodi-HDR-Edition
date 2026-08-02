/*
 *      Copyright (C) 2005-2009 Team XBMC
 *      http://www.xbmc.org
 *
 *		Copyright (C) 2010-2013 Eduard Kytmanov
 *		http://www.avmedia.su
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

#include "DSGraph.h"
#include "DSBlurayNavigator.h"
#include "Filters/DSBluraySource.h"
#include "DSPlayer.h"
#include "Filters/RendererSettings.h"
#include "PixelShaderList.h"
#include "windowing/windows/winsystemwin32.h" //Important needed to get the right hwnd
//#include "windowing/windows/WinSystemWin32DX.h" //important needed to get d3d object and device
#include "Util.h"
#include "ServiceBroker.h"
#include "application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/Settings.h"
#include "FileItem.h"
#include <iomanip>
#include "utils/Log.h"
#include "URL.h"
#include "settings/AdvancedSettings.h"
#include "StreamsManager.h"
#include "GUIInfoManager.h"
#include <streams.h>

#include "FgManager.h"
#include "qnetwork.h"
#include "SComCli.h"
#include "DVDSubtitles/DVDFactorySubtitle.h"
#include "guilib/GUIWindowManager.h"
#include "GUIUserMessages.h"
#include "DSUtil/MediaTypeEx.h"
#include "utils/timeutils.h"
#include "utils/ipinhook.h"
#include "DSInputStreamPVRManager.h"
#include "guilib/LocalizeStrings.h"
#include "cores/DataCacheCore.h"
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "cores/audioengine/engines/activeae/activeae.h"
#include "DSUtil/DSUtil.h"
#include "guilib/GUIComponent.h"

enum
{
  WM_GRAPHNOTIFY = WM_APP + 1,
};

#include "FGManager2.h"

using namespace std;
using namespace KODI::MESSAGING;

CDSGraph* g_dsGraph = NULL;

CDSGraph::CDSGraph(IPlayerCallback& callback)
  : m_pGraphBuilder(NULL)
  , m_iCurrentFrameRefreshCycle(0)
  , m_callback(callback)
  , m_canSeek(-1)
  , m_currentVolume(-1.0f)
  , m_bPerformStop(false)
  , m_bPerformPause(false)

{
  m_DvdState.Clear();
  m_VideoInfo.Clear();
}

CDSGraph::~CDSGraph()
{
}

//This is also creating the Graph
HRESULT CDSGraph::SetFile(const CFileItem& file, const CPlayerOptions &options)
{
  if (CDSPlayer::PlayerState != DSPLAYER_LOADING)
    return E_FAIL;

  HRESULT hr = S_OK;

  m_VideoInfo.Clear();
  m_DvdState.Clear();

  m_pGraphBuilder = new CFGManager2();
  m_pGraphBuilder->InitManager();

  if (SUCCEEDED(m_pGraphBuilder->AddToROT()))
    CLog::Log(LOGDEBUG, "{} Successfully added XBMC to the Running Object Table", __FUNCTION__);
  else
    CLog::Log(LOGERROR, "{} Failed to add XBMC to the Running Object Table", __FUNCTION__);

  START_PERFORMANCE_COUNTER
    hr = m_pGraphBuilder->RenderFileXbmc(file);
  END_PERFORMANCE_COUNTER("Rendering file");

  if (FAILED(hr))
    return hr;

  m_pMediaSeeking = pFilterGraph;
  m_pMediaControl = pFilterGraph;
  m_pMediaEvent = pFilterGraph;
  m_pBasicAudio = pFilterGraph;
  m_pBasicVideo = pFilterGraph;
  m_pVideoWindow = pFilterGraph;
  m_pAMOpenProgress = pFilterGraph;

  // Be sure we are using TIME_FORMAT_MEDIA_TIME
  hr = m_pMediaSeeking->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
  m_VideoInfo.time_format = TIME_FORMAT_MEDIA_TIME;
  LONGLONG durati;
  //this is for the dont adjust refresh rate if video under x minute
  if (SUCCEEDED(m_pMediaSeeking->GetDuration(&durati)))
    CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoDuration = durati;

  // if needed set resolution to match fps then set pixelshader & settings for madVR
  const auto& components = CServiceBroker::GetAppComponents();
  
  components.GetComponent<CApplicationPlayer>()->SetResolution();
  components.GetComponent<CApplicationPlayer>()->SetPixelShader();
  components.GetComponent<CApplicationPlayer>()->RestoreSettings();

  if (m_pVideoWindow)
  {
    m_pVideoWindow->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
    m_pVideoWindow->put_Visible(OATRUE);
    m_pVideoWindow->put_AutoShow(OATRUE);
    m_pVideoWindow->put_WindowState(SW_SHOW);
    m_pVideoWindow->SetWindowForeground(OATRUE);
    m_pVideoWindow->put_MessageDrain((OAHWND)CDSPlayer::GetDShWnd());

  }

  //TODO Ti-Ben
  //with the vmr9 we need to add AM_DVD_SWDEC_PREFER  AM_DVD_VMR9_ONLY on the ivmr9config prefs

  m_VideoInfo.isDVD = CGraphFilters::Get()->IsDVD();

  std::string filterName;
  BeginEnumFilters(pFilterGraph, pEF, pBF)
  {
    if (IsVideoRenderer(pBF)) {
      CGraphFilters::Get()->VideoRenderer.SetFilterInfo(pBF);
    }
    else if (IsAudioWaveRenderer(pBF)) {
      if (!CGraphFilters::Get()->AudioRenderer.pBF)
        CGraphFilters::Get()->AudioRenderer.pBF = pBF;
      CGraphFilters::Get()->AudioRenderer.SetFilterInfo(pBF);
    }

    if (!m_pAMOpenProgress)
    {
      m_pAMOpenProgress = pBF;
    }

    if (!CGraphFilters::Get()->AudioRenderer.osdname.empty() && !CGraphFilters::Get()->VideoRenderer.osdname.empty() && m_pAMOpenProgress)
      break;
  }
  EndEnumFilters
    
    SetVolume(CServiceBroker::GetActiveAE()->GetVolume());

  CDSPlayer::PlayerState = DSPLAYER_LOADED;
  return hr;
}

void CDSGraph::CloseFile()
{
  HRESULT hr;

  if (m_pGraphBuilder)
  {
    if (m_pAMOpenProgress)
      m_pAMOpenProgress->AbortOperation();

    // TEMPORARY: each stage of this teardown has blocked at least once; which one gets
    // reached tells where the next block is
    CLog::Log(LOGDEBUG, "{} waiting for the streams manager", __FUNCTION__);
    if (CStreamsManager::Get())
      CStreamsManager::Get()->WaitUntilReady();

    // A graph that is already stopped has nothing to stop, and asking again is not free:
    // the stop runs through the video renderer, and on some threads that never comes back.
    // A navigated disc arrives here already stopped, by the rebuild that asked for this
    // close.
    if (m_State.current_filter_state != State_Stopped)
    {
      CLog::Log(LOGDEBUG, "{} stopping the graph", __FUNCTION__);
      Stop(true);
      CLog::Log(LOGDEBUG, "{} graph stopped", __FUNCTION__);
    }
    else
      CLog::Log(LOGDEBUG, "{} the graph is already stopped", __FUNCTION__);

    // CSingleExit(m_ObjectLock) removed: nothing ever holds that lock, so it released a
  // mutex this thread did not own and then acquired it for good on scope exit, wedging
  // the next thread through here

    CLog::Log(LOGDEBUG, "{} destroying the streams manager", __FUNCTION__);
    CStreamsManager::Destroy();
    CLog::Log(LOGDEBUG, "{} destroying the chapters manager", __FUNCTION__);
    CChaptersManager::Destroy();
    CLog::Log(LOGDEBUG, "{} managers destroyed", __FUNCTION__);
    g_dsSettings.pixelShaderList->DisableAll();

    m_VideoInfo.Clear();
    m_State.Clear();

    // Stop sending event messages
    if (m_pMediaEvent)
    {
      m_pMediaEvent->SetNotifyWindow((OAHWND)NULL, NULL, NULL);
    }

    /* delete filters */
    CLog::Log(LOGDEBUG, "{} Deleting filters ...", __FUNCTION__);
    CGraphFilters::Destroy();
    CLog::Log(LOGDEBUG, "{} ... done!", __FUNCTION__);
    CGraphFilters::Get()->DVD.Clear();

    if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_EXITMADVRFULLSCREEN))
    {
      g_application.GetComponent<CApplicationPlayer>()->EnableExclusive(false);
    }

    // TEMPORARY: releasing the graph takes the video renderer down with it, which is where
    // a close on the wrong thread stops coming back
    CLog::Log(LOGDEBUG, "{} releasing the filter graph", __FUNCTION__);
    pFilterGraph.Release();
    CLog::Log(LOGDEBUG, "{} filter graph released", __FUNCTION__);

    m_pVideoWindow.Release();
    m_pMediaControl.Release();
    m_pMediaEvent.Release();
    m_pMediaSeeking.Release();
    m_pBasicAudio.Release();
    m_pBasicVideo.Release();
    m_pDvdState.Release();
    m_pAMOpenProgress.Release();

    CLog::Log(LOGDEBUG, "{} deleting the graph builder", __FUNCTION__);
    hr = m_pGraphBuilder->RemoveFromROT();
    SAFE_DELETE(m_pGraphBuilder);
    CLog::Log(LOGDEBUG, "{} graph builder deleted", __FUNCTION__);
  }
  else
  {
    CLog::Log(LOGDEBUG, "{} CloseFile called more than one time!", __FUNCTION__);
  }
}

void CDSGraph::UpdateTime()
{
  // Not sure if it's needed
  //if (m_threadID != GetCurrentThreadId())
  //{
  //  CDSPlayer::PostMessage(new CDSMsg(CDSMsg::PLAYER_UPDATE_TIME));
    //return;
  //}
  
  // CSingleExit(m_ObjectLock) removed: nothing ever holds that lock, so it released a
  // mutex this thread did not own and then acquired it for good on scope exit, wedging
  // the next thread through here

  if (!m_pMediaSeeking)
    return;
  
  LONGLONG Position;
#if TODO
  if (g_pPVRStream)
  { 
    // Current Playback Time
    if (SUCCEEDED(m_pMediaSeeking->GetPositions(&Position, NULL)))
      m_State.time = Position;

    // Total Playback Time
    if (SUCCEEDED(m_pMediaSeeking->GetDuration(&Position)))
      m_State.time_total = Position;

    if (g_PVRManager.IsPlayingTV() && g_windowManager.IsWindowActive(WINDOW_DIALOG_VIDEO_OSD))
    {
      // LiveTV EPG time
      m_State.time_live = MSEC_TO_DS_TIME(g_pPVRStream->GetTime());
      m_State.time_total_live = MSEC_TO_DS_TIME(g_pPVRStream->GetTotalTime());
      m_State.isPVR = true;
    }
    else
    {
      m_State.isPVR = false;
    }
	
    return;
  }
#endif
  m_State.isPVR = false;

  if (SUCCEEDED(m_pMediaSeeking->GetPositions(&Position, NULL)))
  {
    m_State.time = Position;
    
  }

  // Update total time of video.
  // Duration time may increase during playback of in-progress recordings
  UpdateTotalTime();

  //On dvd playback the current time is received in the handlegraphevent
  if (m_VideoInfo.isDVD)
  {
    CStreamsManager::Get()->UpdateDVDStream();
    return;
  }

  if (m_pAMOpenProgress) {
    int64_t t = 0, c = 0;
    if (SUCCEEDED(m_pAMOpenProgress->QueryProgress(&t, &c)) && t > 0 && c < t) {
      m_State.cache_offset = (double)c / t;
    }
  }

  CChaptersManager::Get()->UpdateChapters(m_State.time);
  
}

void CDSGraph::UpdateDvdState()
{
}

void CDSGraph::UpdateTotalTime()
{
  HRESULT hr = S_OK;
  LONGLONG Duration = 0;

  if (m_VideoInfo.isDVD)
  {
    if (!CGraphFilters::Get()->DVD.dvdInfo)
      return;

    REFERENCE_TIME rtDur = 0;
    DVD_HMSF_TIMECODE tcDur;
    ULONG ulFlags;
    if (SUCCEEDED(CGraphFilters::Get()->DVD.dvdInfo->GetTotalTitleTime(&tcDur, &ulFlags)))
    {
      rtDur = HMSF2RT(tcDur);
      m_State.time_total = rtDur;
    }
  }
  else
  {
    if (!m_pMediaSeeking)
      return;

    if (SUCCEEDED(m_pMediaSeeking->GetDuration(&Duration)))
      m_State.time_total = Duration;
  }
}

void CDSGraph::UpdateWindowPosition()
{
  if (m_pVideoWindow && m_pVideoWindow)
  {
    HRESULT hr;
    Com::SmartRect windowRect;
    CRect vr;
    CRect videoRect;
    vr = CServiceBroker::GetWinSystem()->GetGfxContext().GetViewWindow();
    videoRect = CRect(vr);
    videoRect.Intersect(CRect(0, 0, vr.Width(), vr.Height()));
    if (vr.Width() > 0)
    {
      hr = m_pBasicVideo->SetDefaultSourcePosition();

      hr = m_pBasicVideo->SetDestinationPosition(videoRect.x1, videoRect.y1, videoRect.Width(), videoRect.Height());
      hr = m_pVideoWindow->SetWindowPosition(vr.x1, vr.y1, vr.Width(), vr.Height());
    }
  }
}

void CDSGraph::UpdateState()
{
  if (m_pMediaControl == nullptr)
    return;

  HRESULT hr = S_OK;
  if (CDSPlayer::PlayerState == DSPLAYER_CLOSING || CDSPlayer::PlayerState == DSPLAYER_CLOSED)
    return;

  hr = m_pMediaControl->GetState(100, (OAFilterState *)&m_State.current_filter_state);

  //VFW_S_CANT_CUE is graph paused and failed to request the state
  if (hr == VFW_S_CANT_CUE)
  {
    CDSPlayer::PlayerState = DSPLAYER_PAUSED;
    return;
  }

  if (CDSPlayer::PlayerState == DSPLAYER_CLOSING ||
    CDSPlayer::PlayerState == DSPLAYER_CLOSED)
    return;

  switch (m_State.current_filter_state)
  {
  case State_Running:
    CDSPlayer::PlayerState = DSPLAYER_PLAYING;
    break;
  case State_Paused:
    CDSPlayer::PlayerState = DSPLAYER_PAUSED;
    break;
  case State_Stopped:
    CDSPlayer::PlayerState = DSPLAYER_STOPPED;
    break;
  }
}

HRESULT CDSGraph::HandleGraphEvent()
{
  LONG evCode;
  LONG_PTR evParam1, evParam2;
  HRESULT hr = S_OK;

  if (g_application.GetComponent<CApplicationPlayer>()->ReadyDS() && m_bPerformStop)
  {
    m_bPerformStop = false;
    CServiceBroker::GetAppMessenger()->SendMsg(TMSG_MEDIA_STOP);
    CLog::Log(LOGDEBUG, "{} Playback stopped at start", __FUNCTION__);
  }

  if (g_application.GetComponent<CApplicationPlayer>()->ReadyDS() && m_bPerformPause)
  {
    m_bPerformPause = false;
    CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_PAUSE);
    CLog::Log(LOGDEBUG, "{} Playback paused at start", __FUNCTION__);
  }

  // Make sure that we don't access the media event interface
  // after it has already been released.
  if (!m_pMediaEvent)
    return E_POINTER;

  // Process all queued events
  while ((CDSPlayer::PlayerState != DSPLAYER_CLOSING && CDSPlayer::PlayerState != DSPLAYER_CLOSED)
    && SUCCEEDED(m_pMediaEvent->GetEvent(&evCode, &evParam1, &evParam2, 0)))
  {
    switch (evCode)
    {
    case EC_STEP_COMPLETE:
      CLog::Log(LOGDEBUG, "{} EC_STEP_COMPLETE", __FUNCTION__);
      CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_STOP);
      break;
    case EC_COMPLETE:
      CLog::Log(LOGDEBUG, "{} EC_COMPLETE", __FUNCTION__);
      // On a navigated Blu-ray the disc decides when playback is over, not the graph: the
      // graph finishing only means the programme it was built on has run out. Keep the disc
      // moving instead, and the playlist change that follows has the graph built again on
      // whatever the disc plays next.
      if (CDSBlurayNavigator::Get() && !CDSBlurayNavigator::Get()->Finished())
      {
        CDSBlurayStream::FollowTheDisc();
        break;
      }
      m_State.eof = true;
      CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_STOP);
      break;
    case EC_BUFFERING_DATA:
      CLog::Log(LOGDEBUG, "{} EC_BUFFERING_DATA", __FUNCTION__);
      break;
    case EC_USERABORT:
      CLog::Log(LOGDEBUG, "{} EC_USERABORT", __FUNCTION__);
      CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_STOP);
      break;
    case EC_ERRORABORT:
    case EC_ERRORABORTEX:
      if (evParam2)
      {
        std::string error;
        g_charsetConverter.wToUTF8((std::wstring)((BSTR)evParam2), error);
        CLog::Log(LOGDEBUG, "{} EC_ERRORABORT. Error code: 0x%X; Error message: {}", __FUNCTION__, evParam1, error.c_str());
      }
      else
        CLog::Log(LOGDEBUG, "{} EC_ERRORABORT. Error code: 0x%X", __FUNCTION__, evParam1);
      CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_STOP);
      break;
    case EC_STATE_CHANGE:
      CLog::Log(LOGDEBUG, "{} EC_STATE_CHANGE", __FUNCTION__);
      break;
    case EC_DEVICE_LOST:
      CLog::Log(LOGDEBUG, "{} EC_DEVICE_LOST", __FUNCTION__);
      break;
    case EC_VMR_RECONNECTION_FAILED:
      CLog::Log(LOGDEBUG, "{} EC_VMR_RECONNECTION_FAILED", __FUNCTION__);
      break;
    case EC_DVD_CURRENT_HMSF_TIME:
    {
      double fps = evParam2 == DVD_TC_FLAG_25fps ? 25.0
        : evParam2 == DVD_TC_FLAG_30fps ? 30.0
        : evParam2 == DVD_TC_FLAG_DropFrame ? 29.97
        : 25.0;

      m_State.time = HMSF2RT(*((DVD_HMSF_TIMECODE*)&evParam1), fps);;

      break;
    }
    case EC_DVD_TITLE_CHANGE:
    {
      m_pDvdStatus.DvdTitleId = (ULONG)evParam1;
    }
      break;
    case EC_DVD_DOMAIN_CHANGE:
    {
      m_pDvdStatus.DvdDomain = (DVD_DOMAIN)evParam1;
      std::string Domain("-");

      switch (m_pDvdStatus.DvdDomain)
      {
      case DVD_DOMAIN_FirstPlay:

        if (CGraphFilters::Get()->DVD.dvdInfo && SUCCEEDED(CGraphFilters::Get()->DVD.dvdInfo->GetDiscID(NULL, &m_pDvdStatus.DvdGuid)))
        {
          if (m_pDvdStatus.DvdTitleId != 0)
          {
            //s.NewDvd (llDVDGuid);
            // Set command line position
            CGraphFilters::Get()->DVD.dvdControl->PlayTitle(m_pDvdStatus.DvdTitleId, DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
            if (m_pDvdStatus.DvdChapterId > 1)
              CGraphFilters::Get()->DVD.dvdControl->PlayChapterInTitle(m_pDvdStatus.DvdTitleId, m_pDvdStatus.DvdChapterId, DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
            else
            {
              // Trick : skip trailers with somes DVDs
              CGraphFilters::Get()->DVD.dvdControl->Resume(DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
              CGraphFilters::Get()->DVD.dvdControl->PlayAtTime(&m_pDvdStatus.DvdTimecode, DVD_CMD_FLAG_Flush, NULL);
            }

            //m_iDVDTitle	  = s.lDVDTitle;
            m_pDvdStatus.DvdTitleId = 0;
            m_pDvdStatus.DvdChapterId = 0;
          }
          /*else if (!s.NewDvd (llDVDGuid) && s.fRememberDVDPos)
          {
          // Set last remembered position (if founded...)
          DVD_POSITION*	DvdPos = s.CurrentDVDPosition();

          m_pDvdControl2->PlayTitle(DvdPos->lTitle, DVD_CMD_FLAG_Block|DVD_CMD_FLAG_Flush, NULL);
          m_pDvdControl2->Resume(DVD_CMD_FLAG_Block|DVD_CMD_FLAG_Flush, NULL);
          if (SUCCEEDED (hr = m_pDvdControl2->PlayAtTime (&DvdPos->Timecode, DVD_CMD_FLAG_Flush, NULL)))
          {
          m_iDVDTitle = DvdPos->lTitle;
          }
          }*/
        }
        Domain = "First Play";
        m_DvdState.isInMenu = false;
        break;
      case DVD_DOMAIN_VideoManagerMenu:
        Domain = "Video Manager Menu";
        m_DvdState.isInMenu = true;
        break;
      case DVD_DOMAIN_VideoTitleSetMenu:
        Domain = "Video Title Set Menu";
        //Entered menu
        m_DvdState.isInMenu = true;
        break;
      case DVD_DOMAIN_Title:
        Domain = StringUtils::Format("Title %d", m_pDvdStatus.DvdTitleId);
        //left menu
        m_DvdState.isInMenu = false;
        m_pDvdStatus.DvdTitleId = (ULONG)evParam2;
        break;
      case DVD_DOMAIN_Stop:
        Domain = "stopped";
        break;
      default: Domain = "-"; break;
      }
    }
      break;
    case EC_DVD_ERROR:
      break;
    }
    if (m_pMediaEvent)
      hr = m_pMediaEvent->FreeEventParams(evCode, evParam1, evParam2);
  }

  return hr;
}

//USER ACTIONS
void CDSGraph::SetVolume(float nVolume)
{
  // CSingleExit(m_ObjectLock) removed: nothing ever holds that lock, so it released a
  // mutex this thread did not own and then acquired it for good on scope exit, wedging
  // the next thread through here

  if (m_pBasicAudio && (nVolume != m_currentVolume))
  {
    m_pBasicAudio->put_Volume((nVolume == 0.0f) ? -10000 : (nVolume - 1) * 6000);
    m_currentVolume = nVolume;
  }
}

void CDSGraph::Stop(bool rewind)
{
  if (!CDSPlayer::IsDSPlayerThread())
  {
    // TEMPORARY: whether this wait comes back tells whether the player thread is still
    // taking messages
    CLog::Log(LOGDEBUG, "{} - marshalling the stop to the player thread", __FUNCTION__);
    CDSPlayer::PostMessage(new CDSMsgBool(CDSMsg::PLAYER_STOP, rewind));
    CLog::Log(LOGDEBUG, "{} - the player thread finished the stop", __FUNCTION__);
    return;
  }

  // TEMPORARY: the renderer is involved in stopping, and on the wrong thread that never
  // comes back; each stage logged until the stop is trusted again
  CLog::Log(LOGDEBUG, "{} - stopping the graph inline (rewind {})", __FUNCTION__, rewind);

  // CSingleExit(m_ObjectLock) removed: nothing ever holds that lock, so it released a
  // mutex this thread did not own and then acquired it for good on scope exit, wedging
  // the next thread through here

  LONGLONG pos = 0;

  if (m_pMediaControl)
  {
    if (m_pMediaControl->Stop() == S_FALSE)
    {
      do
      {
        m_pMediaControl->GetState(100, (OAFilterState *)&m_State.current_filter_state);
      } while (m_State.current_filter_state != State_Stopped);
    }
  }

  CLog::Log(LOGDEBUG, "{} - the graph reports itself stopped", __FUNCTION__);

  UpdateState();

  // A navigated disc cannot be rewound: it is wherever its navigation put it, and asking
  // the splitter to go back to the start stalls on a stream that only moves forward
  if (rewind && m_pMediaSeeking && !CDSBlurayNavigator::Get())
    m_pMediaSeeking->SetPositions(&pos, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);

  if (!m_pGraphBuilder)
    return;

  BeginEnumFilters(g_dsGraph->pFilterGraph, pEF, pBF)
  {
    Com::SComQIPtr<IFileSourceFilter> pFSF;
    pFSF = Com::SComQIPtr<IAMNetworkStatus, &IID_IAMNetworkStatus>(pBF);
    if (pFSF)
    {
      WCHAR* pFN = NULL;
      AM_MEDIA_TYPE mt;
      if (SUCCEEDED(pFSF->GetCurFile(&pFN, &mt)) && pFN && *pFN)
      {
        pFSF->Load(pFN, NULL);
        CoTaskMemFree(pFN);
        DeleteMediaType(&mt);
      }
      break;
    }
  }
  EndEnumFilters

  CLog::Log(LOGDEBUG, "{} - stop complete", __FUNCTION__);
}

bool CDSGraph::OnMouseClick(tagPOINT pt)
{
  return true;
}

bool CDSGraph::OnMouseMove(tagPOINT pt)
{
  // CSingleExit(m_ObjectLock) removed: nothing ever holds that lock, so it released a
  // mutex this thread did not own and then acquired it for good on scope exit, wedging
  // the next thread through here

  HRESULT hr;
  hr = CGraphFilters::Get()->DVD.dvdControl->SelectAtPosition(pt);
  if (SUCCEEDED(hr))
    return true;
  return true;
}

void CDSGraph::Play(bool force/* = false*/)
{
  if (!CDSPlayer::IsDSPlayerThread())
  {
    CDSPlayer::PostMessage(new CDSMsgBool(CDSMsg::PLAYER_PLAY, force));
    return;
  }
  // CSingleExit(m_ObjectLock) removed: nothing ever holds that lock, so it released a
  // mutex this thread did not own and then acquired it for good on scope exit, wedging
  // the next thread through here
  if (m_pMediaControl && (force || m_State.current_filter_state != State_Running))
    m_pMediaControl->Run();

  if (m_pMediaSeeking)
    SetSpeed(1);

  UpdateState();
}

void CDSGraph::Pause()
{
  if (!CDSPlayer::IsDSPlayerThread())
  {
    CDSPlayer::PostMessage(new CDSMsg(CDSMsg::PLAYER_PAUSE));
    return;
  }

  // CSingleExit(m_ObjectLock) removed: nothing ever holds that lock, so it released a
  // mutex this thread did not own and then acquired it for good on scope exit, wedging
  // the next thread through here
  if (CDSPlayer::PlayerState == DSPLAYER_PAUSED)
  {
    if (m_State.current_filter_state != State_Running)
      m_pMediaControl->Run();
  }
  else
  {
    if (m_pMediaControl)
      if (m_pMediaControl->Pause() == S_FALSE)
      {
      /* the graph may need some time */
      do
      {
        m_pMediaControl->GetState(100, (OAFilterState *)&m_State.current_filter_state);
      } while (m_State.current_filter_state != State_Paused);
      }
  }

  UpdateState();
}

bool CDSGraph::IsPaused() const
{
  return CDSPlayer::PlayerState == DSPLAYER_PAUSED;
}
bool CDSGraph::IsInMenu() const
{
  return m_DvdState.isInMenu;
}
void CDSGraph::SeekInMilliSec(double position)
{
  Seek(MSEC_TO_DS_TIME(position));
}

bool CDSGraph::SetSpeed(double dSpeed)
{
  if (!m_pMediaSeeking)
    return false;

  HRESULT hr = m_pMediaSeeking->SetRate(dSpeed);
  
  return SUCCEEDED(hr);
}

void CDSGraph::Seek(uint64_t position, uint32_t flags /*= AM_SEEKING_AbsolutePositioning*/, bool showPopup /*= true*/)
{
  if (!CDSPlayer::IsDSPlayerThread())
  {
    CDSPlayer::PostMessage(new CDSMsgPlayerSeekTime(position, flags));
    return;
  }
  // CSingleExit(m_ObjectLock) removed: nothing ever holds that lock, so it released a
  // mutex this thread did not own and then acquired it for good on scope exit, wedging
  // the next thread through here
#if 0
  if (g_pPVRStream || CGraphFilters::Get()->UsingMediaPortalTsReader())
  {
    // For live tv and in-progress recordings.
    // When seek position is close to the end of the file.
    uint64_t endOfTimeShiftFile = (GetTotalTime() >= (uint64_t)MSEC_TO_DS_TIME(2000)) ? (GetTotalTime() - (uint64_t)MSEC_TO_DS_TIME(2000)) : 0;
    if (position > endOfTimeShiftFile)
      position = endOfTimeShiftFile;
  }
#endif
  if (showPopup)
    CDataCacheCore::GetInstance().SeekFinished(100000);

  if (!m_pMediaSeeking)
    return;

  if (!m_VideoInfo.isDVD)
  {
    m_pMediaSeeking->SetPositions((LONGLONG *)&position, flags, NULL, AM_SEEKING_NoPositioning);
  }
  else
  {
    if (!CGraphFilters::Get()->DVD.dvdControl)
      return;

    DVD_HMSF_TIMECODE tc = RT2HMSF(position);
    CGraphFilters::Get()->DVD.dvdControl->PlayAtTime(&tc, DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
  }
  int iTime = DS_TIME_TO_MSEC(position);
  int seekOffset = (int)(iTime - DS_TIME_TO_MSEC(GetTime()));
  m_callback.OnPlayBackSeek(iTime, seekOffset);
  if (showPopup)
    CDataCacheCore::GetInstance().SeekFinished(0);
  //if (showPopup)
  //  CServiceBroker::GetGUI()->GetInfoManager().SetDisplayAfterSeek();
}

void CDSGraph::Seek(bool bPlus, bool bLargeStep)
{
  // Chapter support
  if ((CChaptersManager::Get()->HasChapters())
    && (((bPlus && CChaptersManager::Get()->GetChapter() < CChaptersManager::Get()->GetChapterCount())
    || (!bPlus && CChaptersManager::Get()->GetChapter() > 1)) && bLargeStep))
  {
    int chapter = 0;
    if (bPlus)
      chapter = CChaptersManager::Get()->SeekChapter(
      CChaptersManager::Get()->GetChapter() + 1);
    else
      chapter = CChaptersManager::Get()->SeekChapter(
      CChaptersManager::Get()->GetChapter() - 1);

    if (chapter >= 0)
      m_callback.OnPlayBackSeekChapter(chapter);

    return;
  }

  if (!m_pMediaSeeking || !m_pMediaControl)
    return;

  int64_t seek = 0;
  if (CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoUseTimeSeeking && DS_TIME_TO_SEC(GetTotalTime()) > 2 * CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoTimeSeekForwardBig)
  {
    if (bLargeStep)
      seek = bPlus ? CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoTimeSeekForwardBig : CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoTimeSeekBackwardBig;
    else
      seek = bPlus ? CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoTimeSeekForward : CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoTimeSeekBackward;
    seek = GetTime() + SEC_TO_DS_TIME(seek);
  }
  else
  {
    float percent;
    if (bLargeStep)
      percent = (float)(bPlus ? CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoPercentSeekForwardBig : CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoPercentSeekBackwardBig);
    else
      percent = (float)(bPlus ? CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoPercentSeekForward : CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoPercentSeekBackward);
    seek = GetTotalTime() * (float)((GetPercentage() + percent) / 100);
  }

  UpdateTime();
  Seek((seek < 0) ? 0 : seek);
  UpdateTime();
}

void CDSGraph::SeekPercentage(float iPercent)
{
  uint64_t iTotalTime = GetTotalTime();

  if (iTotalTime > 0)
  {
    Seek((uint64_t)iTotalTime * iPercent / 100);
  }
}

// return time in DS_TIME_BASE unit
uint64_t CDSGraph::GetTime(bool forcePlaybackTime)
{
  // Returns Playback Time or LiveTV EPG Time, depends on player mode(LiveTV video or none PVR video) and OSD state
  return (CDSPlayer::IsDSPlayerThread() || forcePlaybackTime || !m_State.isPVR) ? m_State.time : m_State.time_live;
}

// return length in DS_TIME_BASE unit
uint64_t CDSGraph::GetTotalTime(bool forcePlaybackTime)
{
  // Returns Playback Time or LiveTV EPG Time, depends on player mode(LiveTV video or none PVR video) and OSD state
  return (CDSPlayer::IsDSPlayerThread() || forcePlaybackTime || !m_State.isPVR) ? m_State.time_total : m_State.time_total_live;
}

float CDSGraph::GetPercentage()
{
  uint64_t iTotalTime = GetTotalTime();

  if (iTotalTime)
  {
    return (GetTime() * 100.0f / (float)iTotalTime);
  }
  return 0.0f;
}

float CDSGraph::GetCachePercentage()
{
  return (m_State.cache_offset * 100) - GetPercentage();
}

std::string CDSGraph::GetGeneralInfo()
{
  std::string generalInfo, info;

  BeginEnumFilters(g_dsGraph->pFilterGraph, pEF, pBF)
  {
    if (pBF == CGraphFilters::Get()->AudioRenderer.pBF || pBF == CGraphFilters::Get()->VideoRenderer.pBF)
      continue;

    // force osdname for XySubFilter
    if ((pBF == CGraphFilters::Get()->Subs.pBF) && CGraphFilters::Get()->Subs.osdname != "")
      info = CGraphFilters::Get()->Subs.osdname;
    else
      g_charsetConverter.wToUTF8(GetFilterName(pBF).c_str(), info);
    if (!info.empty())
      generalInfo.empty() ? generalInfo += "" + info : generalInfo += ", " + info;
  }
  EndEnumFilters

    return generalInfo;
}

std::string CDSGraph::GetAudioInfo()
{
  std::string audioInfo;
  CStreamsManager *c = CStreamsManager::Get();

  if (!c)
    return "File closed";

  if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SHOWSPLITTERDETAIL) ||
      CGraphFilters::Get()->UsingMediaPortalTsReader())
  {
    audioInfo = StringUtils::Format("Audio: ({}, %d Hz, %d Channels) | Renderer: {}",
      c->GetAudioCodecDisplayName(g_application.GetComponent<CApplicationPlayer>()->GetAudioStream()).c_str(),
      c->GetSampleRate(g_application.GetComponent<CApplicationPlayer>()->GetAudioStream()),
      c->GetChannels(g_application.GetComponent<CApplicationPlayer>()->GetAudioStream()),
      CGraphFilters::Get()->AudioRenderer.osdname.c_str());
  }
  else
  {
    std::string strStreamName;
    c->GetAudioStreamName(g_application.GetComponent<CApplicationPlayer>()->GetAudioStream(),strStreamName);
    audioInfo = StringUtils::Format("Audio: ({}) | Renderer: {}",
      strStreamName.c_str(),
      CGraphFilters::Get()->AudioRenderer.osdname.c_str());
  }

  int iAudioDelay = round(-CStreamsManager::Get()->GetAVDelay() * 1000.0f);
  audioInfo = StringUtils::Format("{} | Delay: %ims",
    audioInfo.c_str(),
    iAudioDelay);

  return audioInfo;
}

std::string CDSGraph::GetVideoInfo()
{
  std::string videoInfo = "";
  CStreamsManager *c = CStreamsManager::Get();

  if (!c)
    return "File closed";
  
  if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SHOWSPLITTERDETAIL) ||
      CGraphFilters::Get()->UsingMediaPortalTsReader())
  {
    videoInfo = StringUtils::Format("Video: ({}, %dx%d) | Renderer: {}",
      c->GetVideoCodecDisplayName().c_str(),
      c->GetPictureWidth(),
      c->GetPictureHeight(),
      CGraphFilters::Get()->VideoRenderer.osdname.c_str());
  } 
  else
  {
    std::string strStreamName;
    c->GetVideoStreamName(strStreamName);
    videoInfo = StringUtils::Format("Video: ({}) | Renderer: {}",
      strStreamName.c_str(), 
      CGraphFilters::Get()->VideoRenderer.osdname.c_str());
  }

  if (!m_pStrCurrentFrameRate.empty())
    videoInfo += m_pStrCurrentFrameRate.c_str();

  std::string strDXVA;
  if (!g_application.GetComponent<CApplicationPlayer>()->UsingDS(DIRECTSHOW_RENDERER_MADVR))
    strDXVA = GetDXVADecoderDescription();

  if (!strDXVA.empty())
    videoInfo += " | " + strDXVA;

  return videoInfo;
}

bool CDSGraph::CanSeek()
{
  if (CDSPlayer::PlayerState == DSPLAYER_CLOSING || CDSPlayer::PlayerState == DSPLAYER_CLOSED)
    return false;

  //Dvd are not using the interface IMediaSeeking for seeking
  //if the filter dont support seeking you would get VFW_E_DVD_OPERATION_INHIBITED on the PlayAtTime
  if (m_VideoInfo.isDVD)
    return true;

  if (m_canSeek > -1)
    return (m_canSeek == 1);

  if (!m_pMediaSeeking)
    return false;

  DWORD seekcaps = AM_SEEKING_CanSeekForwards
    | AM_SEEKING_CanSeekBackwards
    | AM_SEEKING_CanSeekAbsolute;

  // Cache seeking capabilities. It seems to hang if it's called too quickly
  if SUCCEEDED(m_pMediaSeeking->CheckCapabilities(&seekcaps))
    m_canSeek = 1;
  else
    m_canSeek = 0;

  return (m_canSeek == 1);
}

#endif
