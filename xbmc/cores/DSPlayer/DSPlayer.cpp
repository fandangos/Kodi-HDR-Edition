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

#include "DSPlayer.h"
#include "DSBlurayNavigator.h"
#include "Filters/DSBluraySource.h"
#include "DSUtil/DSUtil.h" // unload loaded filters
#include "SComCli.h"
#include "Filters/RendererSettings.h"

#include "windowing/windows/winsystemwin32.h" //Important needed to get the right hwnd
#include "xbmc/GUIInfoManager.h"
#include "utils/SystemInfo.h"
#include "input/actions/Action.h"
#include "input/actions/ActionIDs.h"
#include "input/mouse/MouseStat.h"
#include "settings/Settings.h"
#include "FileItem.h"
#include "utils/log.h"
#include "utils/XTimeUtils.h"
#include "URL.h"

#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogBusy.h"
#include "windowing/windows/WinSystemWin32DX.h"
#include "dialogs/GUIDialogOK.h"
#include "PixelShaderList.h"
#include "guilib/LocalizeStrings.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "video/windows/GUIWindowVideoBase.h"
#include "cores/AudioEngine/Engines/ActiveAE/ActiveAE.h"
#include "messaging/ApplicationMessenger.h"
#include "DSInputStreamPVRManager.h"
#include "pvr/PVRManager.h"
#include "pvr/windows/GUIWindowPVRBase.h"
#include "pvr/channels/PVRChannel.h"
#include "settings/AdvancedSettings.h"
#include "Application/Application.h"
#include "GUIUserMessages.h"
#include "input/keyboard/Key.h"
#include "settings/MediaSettings.h"
#include "settings/MediaSourceSettings.h"
#include "cores/DSPlayer/dsgraph.h"
#include "settings/MediaSettings.h"
#include "settings/DisplaySettings.h"
#include "ServiceBroker.h"
#include "cores/DataCacheCore.h"
#include "DSFilterVersion.h"
#include "DVDFileInfo.h"

#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "Cores/VideoPlayer/Interface/TimingConstants.h"
#include "guilib/GUIComponent.h"
#include "input/actions//ActionIDs.h"
#include "application/ApplicationPlayer.h"
#include "application/AppInboundProtocol.h"
#include "video/VideoFileItemClassify.h"
#include "processthreadsapi.h"
#include "utils/URIUtils.h"

using namespace PVR;
using namespace std::chrono_literals;
using namespace std;
using namespace KODI::MESSAGING;

DSPLAYER_STATE CDSPlayer::PlayerState = DSPLAYER_CLOSED;
std::atomic<bool> CDSPlayer::m_discProgrammeChanged{false};
std::atomic<int64_t> CDSPlayer::m_discSeekWanted{-1};
std::atomic<int> CDSPlayer::m_discAudioWanted{-1};
std::atomic<int> CDSPlayer::m_discSubtitleWanted{-1};
std::atomic<int> CDSPlayer::m_discSubtitlesVisibleWanted{-1};
std::atomic<int> CDSPlayer::m_discInputForced{-1};
bool CDSPlayer::m_keepDiscOpen = false;
CGUIDialogBoxBase *CDSPlayer::errorWindow = NULL;
ThreadIdentifier CDSPlayer::m_threadID = 0;
HWND CDSPlayer::m_hWnd = 0;

CDSPlayer::CDSPlayer(IPlayerCallback& callback)
  : IPlayer(callback),
  CThread("CDSPlayer thread"),
  m_hReadyEvent(true),
  m_pGraphThread(this),
  m_bEof(false),
  m_renderManager(this),
  m_outboundEvents(std::make_unique<CJobQueue>(false, 1, CJob::PRIORITY_NORMAL))
{
  m_CurrentVideoRenderer = DIRECTSHOW_RENDERER_UNDEF;
  m_pAllocatorCallback = nullptr;
  m_pSettingCallback = nullptr;
  m_pPaintCallback = nullptr;
  m_renderOnDs = false;
  m_bPerformStop = false;
  m_lastActiveVideoRect = { 0, 0, 0, 0 };

  m_canTempo = false;
  m_HasVideo = false;
  m_HasAudio = false;
  m_isMadvr = (StringUtils::ToLower(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_VIDEORENDERER)) == "madvr");
  
  if (m_isMadvr)
  {
    if (InitWindow(m_hWnd))
      CLog::Log(LOGDEBUG, "{} : Create DSPlayer window - hWnd:", __FUNCTION__);
  }

  /* Suspend AE temporarily so exclusive or hog-mode sinks */
  /* don't block DSPlayer access to audio device  */
  IAE* audioengine;
  audioengine = CServiceBroker::GetActiveAE();
  if (audioengine)
  {
    if (!audioengine->Suspend())
    {
      CLog::Log(LOGINFO, "{}: Failed to suspend AudioEngine before launching external program",
        __FUNCTION__);
    }
  }

  CoInitializeEx(NULL, COINIT_MULTITHREADED);

  m_pClock.GetClock(); // Reset the clock
  g_dsGraph = new CDSGraph(callback);

  // Change DVD Clock, time base
  m_pClock.SetTimeBase((int64_t)DS_TIME_BASE);

  m_processInfo = CProcessInfo::CreateInstance();
  m_processInfo->SetDataCache(&CServiceBroker::GetDataCacheCore());
  
  m_processInfo->SetMadvr(m_isMadvr);
  
  CServiceBroker::GetWinSystem()->Register(this);
}

CDSPlayer::~CDSPlayer()
{
  CServiceBroker::GetWinSystem()->Unregister(this);
  
  /* Resume AE processing of XBMC native audio */
  if (!CServiceBroker::GetActiveAE()->Resume())
  {
    CLog::Log(LOGFATAL, __FUNCTION__, "Failed to restart AudioEngine after return from DSPlayer");
  }

  if (PlayerState != DSPLAYER_CLOSED)
    CloseFile();

  UnloadExternalObjects();
  CLog::Log(LOGDEBUG, "{} External objects unloaded", __FUNCTION__);

  // Restore DVD Player time base clock
  m_pClock.SetTimeBase(DVD_TIME_BASE);

  // Save Shader settings
  g_dsSettings.pixelShaderList->SaveXML();

  CoUninitialize();

  DeInitWindow();

  SAFE_DELETE(g_dsGraph);
#if TODO
  SAFE_DELETE(g_pPVRStream);
#endif

  SetVisibleScreenArea(m_lastActiveVideoRect);

  while (m_outboundEvents->IsProcessing())
  {
    CThread::Sleep(10ms);
  }

  CLog::Log(LOGINFO, "{} DSPlayer is now closed", __FUNCTION__);
}

int CDSPlayer::GetSubtitleCount() const
{
  return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSubtitleCount() : 0;
}

int CDSPlayer::GetSubtitle()
{
  return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSubtitle() : 0;
}

void CDSPlayer::SetSubtitle(int iStream)
{
  CLog::Log(LOGINFO, "{} settings subtitle #{}",__FUNCTION__, iStream);
   if (CStreamsManager::Get()) 
     CStreamsManager::Get()->SetSubtitle(iStream);
}

bool CDSPlayer::GetSubtitleVisible() const
{
  return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSubtitleVisible() : true;
}

void CDSPlayer::SetSubtitleVisible(bool bVisible)
{
  if (CStreamsManager::Get())
    CStreamsManager::Get()->SetSubtitleVisible(bVisible);
  m_processInfo->GetVideoSettingsLocked().SetSubtitleVisible(bVisible);
}

void CDSPlayer::AddSubtitle(const std::string& strSubPath) 
{
  if (CStreamsManager::Get())
    CStreamsManager::Get()->SetSubtitle(CStreamsManager::Get()->AddSubtitle(strSubPath));
}

bool CDSPlayer::WaitForFileClose()
{
  StopThread(true);
  //if (!WaitForThreadExit(100) || !m_pGraphThread.WaitForThreadExit(100))
  //  return false;

  return true;
}

bool CDSPlayer::OpenFileInternal(const CFileItem& file)
{
  m_processInfo->ResetVideoCodecInfo();
  m_processInfo->ResetAudioCodecInfo();
  try
  {
    CLog::Log(LOGINFO, "{} - DSPlayer: Opening: {}", __FUNCTION__, CURL::GetRedacted(file.GetPath()).c_str());
    if (PlayerState != DSPLAYER_CLOSED)
      CloseFile();

    if(!WaitForFileClose())
      return false;

    // Any closes on the way here were making room for this open; from now on a close is a
    // real stop again and finishes with the disc
    m_keepDiscOpen = false;

    PlayerState = DSPLAYER_LOADING;
    m_currentFileItem = file;

    m_hReadyEvent.Reset();
    SetCurrentVideoRenderer(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_VIDEORENDERER));

    m_renderManager.PreInit(m_CurrentVideoRenderer);

    //if (m_pAllocatorCallback)
      //m_pAllocatorCallback->SetRenderManager(m_renderManager);
    Create();

    // wait for the ready event
    CGUIDialogBusy::WaitOnEvent(m_hReadyEvent, CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_videoBusyDialogDelay_ms, false);

    return (PlayerState != DSPLAYER_ERROR);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} - Exception thrown on open", __FUNCTION__);
    return false;
  }
}

void CDSPlayer::LoadVideoSettings(const CFileItem& file)
{
  CMediaSettings::GetInstance().GetAtStartVideoSettings() = CMediaSettings::GetInstance().GetDefaultVideoSettings();

  CDSPlayerDatabase dsdbs;
  if (!dsdbs.Open())
    return;

  CFileItem fileItem = file;
  std::string sUrl = CURL::GetRedacted(file.GetPath()).c_str();

  if (!fileItem.HasVideoInfoTag() || !fileItem.GetVideoInfoTag()->HasStreamDetails())
  {
    CLog::Log(LOGDEBUG, "{} - trying to extract filestream details from video file {}", __FUNCTION__, sUrl.c_str());
    CDVDFileInfo::GetFileStreamDetails(&fileItem);
  }

  CStreamDetails streamDetails = fileItem.GetVideoInfoTag()->m_streamDetails;
  CMadvrSettings &madvrSettings = CMediaSettings::GetInstance().GetCurrentMadvrSettings();

  madvrSettings.m_Resolution = VideoDimsToResolution(streamDetails.GetVideoWidth(), streamDetails.GetVideoHeight());
  madvrSettings.m_TvShowName = fileItem.GetVideoInfoTag()->m_strShowTitle;

  // Load stored files settings
  if (dsdbs.GetVideoSettings(fileItem.GetPath().c_str(), madvrSettings))
  {
    CLog::Log(LOGDEBUG, "Loaded madVR for file settings for {}", sUrl.c_str());
  }
  // if not present Load stored TvShowName settings
  else if (dsdbs.GetTvShowSettings(madvrSettings.m_TvShowName, madvrSettings))
  {
    CLog::Log(LOGDEBUG, "Loaded madVR for tvshow {} settings for {}", madvrSettings.m_TvShowName.c_str(), sUrl.c_str());
  }
  // if not present Load stored Resolution settings
  else if (dsdbs.GetResSettings(madvrSettings.m_Resolution, madvrSettings))
  {
    CLog::Log(LOGDEBUG, "Loaded madVR for resolution {}p settings for {}", madvrSettings.m_Resolution, sUrl.c_str());
  }
  // if not present Load stored for all setting
  else if (dsdbs.GetResSettings(MADVR_RES_ALL, madvrSettings))
  {
    CLog::Log(LOGDEBUG, "Loaded madVR for all settings for {}", sUrl.c_str());
  }
  // restore default settings
  else
  {
    CLog::Log(LOGDEBUG, "Restored madVR default settings for {}", sUrl.c_str());
    madvrSettings.RestoreDefaultSettings();
  }

  madvrSettings.StoreAtStartSettings();

  dsdbs.Close();
}

void CDSPlayer::SetCurrentVideoRenderer(const std::string &videoRenderer)
{
  
  if (StringUtils::ToLower(videoRenderer) == "madvr")
    m_CurrentVideoRenderer = DIRECTSHOW_RENDERER_MADVR;
  if (StringUtils::ToLower(videoRenderer) == "mpcvr")
    m_CurrentVideoRenderer = DIRECTSHOW_RENDERER_MPCVR;
}

bool CDSPlayer::OpenFile(const CFileItem& file, const CPlayerOptions &options)
{
  if (m_isMadvr && !CDSFilterVersion::Get()->IsRegisteredFilter(MADVR_FILTERSTR))
  {
    CLog::Log(LOGDEBUG, "{} - madVR it's not installed on the system pls download it before to use it with DSPlayer", __FUNCTION__);

    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, g_localizeStrings.Get(90023), g_localizeStrings.Get(90024), 6000L, false);
    return false;
  }

  if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOSCREEN_FAKEFULLSCREEN) && m_isMadvr)
  {
    BOOL bFullScreen;
    DX::DeviceResources::Get()->GetSwapChain()->GetFullscreenState(&bFullScreen, NULL);

    if (!!bFullScreen)
    {
      CServiceBroker::GetWinSystem()->GetGfxContext().SetVideoResolution(
        CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution(), true);
    }
    //CServiceBroker::GetRenderSystem()->SetWindowedForMadvr();
    
    CGraphFilters::Get()->SetKodiRealFS(true);

    CServiceBroker::GetSettingsComponent()->GetSettings()->SetBool(CSettings::SETTING_VIDEOSCREEN_FAKEFULLSCREEN, true);
  }

  CGraphFilters::Get()->SetAuxAudioDelay();

  CLog::Log(LOGINFO, "{} - DSPlayer: Opening: {}", __FUNCTION__, CURL::GetRedacted(file.GetPath()).c_str());

  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_MANAGEMADVRWITHKODI) == KODIGUI_LOAD_DSPLAYER)
    LoadVideoSettings(file);

  CFileItem fileItem = file;

  m_PlayerOptions = options;
  m_canTempo = true;

  if (KODI::VIDEO::IsVideoDb(file))
  {
    fileItem.SetPath(file.GetVideoInfoTag()->m_strFileNameAndPath);
    fileItem.SetProperty("original_listitem_ur", file.GetPath());
  }

  //this fix path with plugin:// in it because they are not always internet stream
  if (fileItem.GetDynPath().length() > 0)
    fileItem.SetPath(fileItem.GetDynPath());

  if (URIUtils::IsInternetStream(fileItem.GetDynPath()))
  {
    CURL url = fileItem.GetDynURL();
    url.SetProtocolOptions("");
    fileItem.SetPath(url.Get());
  }
#if TODO
  else if (fileItem.IsPVR())
  {
    g_pPVRStream = new CDSInputStreamPVRManager(this);
    return g_pPVRStream->Open(fileItem);
    m_canTempo = false;
  }
#endif
  return OpenFileInternal(fileItem);
}

bool CDSPlayer::CloseFile(bool reopen)
{
  // TEMPORARY: which thread runs this close, and whether it is one of the closes that keeps
  // the disc alive, decides which of the known blocks it can run into
  CLog::Log(LOGDEBUG, "{} - closing (reopen {}, keeping the disc open {}, state {})",
            __FUNCTION__, reopen, m_keepDiscOpen, static_cast<int>(PlayerState));

  // CSingleExit(m_CleanSection) removed: it was this section's only user, so it provided
  // no exclusion - it released a mutex this thread never held and then kept it on exit,
  // which wedged the second close of a reopen

  if (PlayerState == DSPLAYER_LOADING)
  {
    g_dsGraph->QueueStop();
    return false;
  }

  // Stopping the graph waits for its streaming threads, and on a navigated disc those sit
  // in reads that block until the disc gives more. Cut them loose first, whatever the
  // close is for, or the stop waits on a read that is waiting on a menu.
  CDSBlurayStream::StopFeedingTheGraph();

  // A disc is kept open across graph rebuilds, so this is where it is really finished with,
  // unless the close is only there to make way for the programme the disc moved on to. The
  // flag is deliberately not consumed here: the way back round to opening the disc again
  // closes the player more than once, and the disc has to survive every one of them. The
  // open clears it once the new player is truly under way.
  if (!m_keepDiscOpen)
  {
    // This close is the end of the disc, not a step towards its next programme. Ending the
    // session shuts BD-J down, and a disc being shut down leaves its playlist like any other
    // move - which the graph management thread would answer by opening the disc all over
    // again, so pressing stop started the film instead of ending it. Stop asking the disc
    // what it wants before it is touched, and drop anything it asked for on the way in.
    if (CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get())
      navigator->SuspendProgrammeChanges();
    TakeDiscProgrammeChanged();

    CDSBlurayNavigator::EndSession();
  }

  // zoom
  if (m_pAllocatorCallback)
    m_lastActiveVideoRect = m_pAllocatorCallback->GetActiveVideoRect();

  // if needed restore the currentrate
  if (m_pGraphThread.GetCurrentRate() != 1)
    SetSpeed(1);

  // reset intial delay in decoder interface
  if (CStreamsManager::Get()) CStreamsManager::Get()->resetDelayInterface();

  if (PlayerState == DSPLAYER_CLOSED || PlayerState == DSPLAYER_CLOSING)
    return true;

  PlayerState = DSPLAYER_CLOSING;
  m_HasVideo = false;
  m_HasAudio = false;
  m_canTempo = false;

  // set the abort request so that other threads can finish up
  m_bEof = g_dsGraph->IsEof();

  // Telling the application playback has ended sets its own teardown in motion, which runs
  // a second close alongside this one. When the close is only making way for the programme
  // the disc moved to, playback has not ended at all.
  //
  // "Ended" and "stopped" are not interchangeable: ended means the file reached its end, and
  // Kodi answers it by opening whatever comes next - which for a disc opened on its own is
  // the same disc again, so pressing stop started it over from first play. Only a close that
  // finds the graph at the end of its stream has ended; every other one is a stop. This is
  // the rule the dead `#if TODO` block in OnExit already spells out, on the path that runs.
  if (!m_keepDiscOpen)
  {
    if (m_bEof)
      m_callback.OnPlayBackEnded();
    else
      m_callback.OnPlayBackStopped();
  }

  // stop the rendering on dsplayer device
  m_renderOnDs = false;

  // Closing the graph destroys the video renderer, and madVR does not finish going away
  // while the application thread is still drawing into the surfaces they share. Make the
  // drawing stop, and know that it has, before anything is taken apart.
  m_renderManager.StopRenderingIntoDirectShow();

  // Taking the graph apart destroys the video renderer, and madVR only finishes going away
  // on the thread that built the graph; asked from anywhere else it never comes back. That
  // only matters when a navigated disc rebuilds its graph, which is driven from the graph
  // management thread — an ordinary stop comes from the application thread, where closing
  // in place has always worked, and where waiting on another thread would be the deadlock
  // madVR is famous for here.
  CLog::Log(LOGDEBUG, "{} - closing the graph", __FUNCTION__);
  if (m_keepDiscOpen && m_threadID && ::GetCurrentThreadId() != m_threadID)
    PostMessage(new CDSMsg(CDSMsg::PLAYER_CLOSE_GRAPH), true);
  else
    g_dsGraph->CloseFile();
  CLog::Log(LOGDEBUG, "{} - graph closed", __FUNCTION__);

  PlayerState = DSPLAYER_CLOSED;

  // Stop threads. Closing can be initiated from the graph thread itself, when a navigated
  // disc moves to another programme, and a thread cannot wait for its own end: ask it to
  // finish without waiting and it falls out of its loop on return.
  CLog::Log(LOGDEBUG, "{} - stopping the graph thread", __FUNCTION__);
  m_pGraphThread.StopThread(!m_pGraphThread.IsCurrentThread());

  // The player thread sits in GetMessage and nothing on this path sends it anything, so
  // waiting for it without waking it first waits forever
  CLog::Log(LOGDEBUG, "{} - stopping the player thread {}", __FUNCTION__, m_threadID);
  if (m_threadID)
    PostThreadMessage(m_threadID, WM_QUIT, 0, 0);
  StopThread();

  CLog::Log(LOGDEBUG, "{} - releasing the renderer", __FUNCTION__);
  m_renderManager.UnInit();

  CLog::Log(LOGDEBUG, "{} File closed (state {})", __FUNCTION__, static_cast<int>(PlayerState));
  return true;
}

void CDSPlayer::GetVideoStreamInfo(int streamId, VideoStreamInfo& info) const
{
  std::unique_lock<CCriticalSection> lock(m_content.m_section);
  std::string strStreamName;

  if (CStreamsManager::Get())
    CStreamsManager::Get()->GetVideoStreamName(strStreamName);

  //if (s.language.length() > 0)
  //  info.language = s.language;

  if (strStreamName.length() > 0)
    info.name = strStreamName;

  m_renderManager.GetVideoRect(info.SrcRect, info.DestRect, info.VideoRect);

  info.name = strStreamName;
  info.width = (GetPictureWidth());
  info.height = (GetPictureHeight());
  info.codecName = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetVideoCodecName() : "";
  info.videoAspectRatio = (float)info.width / (float)info.height;

  info.stereoMode = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetStereoMode() : "";
  if (info.stereoMode == "mono")
    info.stereoMode = "";
}

//old dsplayer might change to this one
void CDSPlayer::GetVideoStreamInfo(int streamId, SPlayerVideoStreamInfo &info) const
{
  std::unique_lock<CCriticalSection> lock(m_content.m_section);
  std::string strStreamName;

  if (CStreamsManager::Get()) CStreamsManager::Get()->GetVideoStreamName(strStreamName);
  info.name = strStreamName;
  info.width = (GetPictureWidth());
  info.height = (GetPictureHeight());
  info.videoCodecName = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetVideoCodecName() : "";
  info.videoAspectRatio = (float)info.width / (float)info.height;
  CRect viewRect;
  m_renderManager.GetVideoRect(info.SrcRect, info.DestRect, viewRect);
  info.stereoMode = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetStereoMode() : "";
  if (info.stereoMode == "mono")
    info.stereoMode = "";
}

void CDSPlayer::GetAudioStreamInfo(int index, AudioStreamInfo& info) const
{
  if (index == CURRENT_STREAM)
    index = CStreamsManager::Get() ? CStreamsManager::Get()->GetAudioStream() : 0;

  CSingleExit lock(m_StateSection);

  std::string strStreamName;
  std::string label;
  std::string codecname;

  info.bitrate = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetBitsPerSample(index) : 0;
  info.codecName = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioCodecName(index) : "";
  if (CStreamsManager::Get()) CStreamsManager::Get()->GetAudioStreamName(index, strStreamName);
  // language must hold an ISO 639 code, not the displayname. The GUI resolves it through
  // g_LangCodeExpander and falls back to "Unknown" whenever the lookup fails
  if (CStreamsManager::Get()) CStreamsManager::Get()->GetAudioStreamLanguage(index, info.language);
  info.channels = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetChannels(index) : 0;
  info.samplerate = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetSampleRate(index) : 0;
  codecname = (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioCodecDisplayName(index) : "";

  if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SHOWSPLITTERDETAIL) ||
      CGraphFilters::Get()->UsingMediaPortalTsReader())
  { 
    label = StringUtils::Format("{} - ({}, %d Hz, {} Channels)", strStreamName.c_str(), codecname.c_str(), info.samplerate, info.channels);
    info.name = label;
  }
  else
    info.name = strStreamName;
}

void CDSPlayer::GetSubtitleStreamInfo(int index, SubtitleStreamInfo& info) const
{
  std::string strStreamName;
  if (CStreamsManager::Get())
  {
    CStreamsManager::Get()->GetSubtitleInfo(index, info);
  }

}

bool CDSPlayer::IsPlaying() const
{
  return !m_bStop;
}

bool CDSPlayer::HasVideo() const
{
  return m_HasVideo;
}
bool CDSPlayer::HasAudio() const
{
  return m_HasAudio;
}

void CDSPlayer::GetDebugInfo(std::string &audio, std::string &video, std::string &general)
{
  audio = g_dsGraph->GetAudioInfo();
  video = g_dsGraph->GetVideoInfo();
  GetGeneralInfo(general);
}

void CDSPlayer::GetGeneralInfo(std::string& strGeneralInfo)
{
  CSingleExit lock(m_StateSection);
  strGeneralInfo = g_dsGraph->GetGeneralInfo();
}

float CDSPlayer::GetAVDelay()
{
  float fValue = 0.0f;

  if (CStreamsManager::Get())
    fValue = CStreamsManager::Get()->GetAVDelay();

  return fValue;
}

void CDSPlayer::SetAVDelay(float fValue)
{
  //get displaylatency
  int iDisplayLatency = m_renderManager.GetDisplayLatency() * 1000;
  m_processInfo->GetVideoSettingsLocked().SetAudioDelay(fValue);
  if (CStreamsManager::Get()) CStreamsManager::Get()->SetAVDelay(fValue,iDisplayLatency);

  SetAudioCodeDelayInfo();
}

void CDSPlayer::SetAudioStream(int iStream) 
{ 
  if (CStreamsManager::Get()) CStreamsManager::Get()->SetAudioStream(iStream); 
    UpdateProcessInfo(iStream);
}

float CDSPlayer::GetSubTitleDelay()
{
  float fValue = 0.0f;

  if (CStreamsManager::Get())
    fValue = CStreamsManager::Get()->GetSubTitleDelay();

  return fValue;
}

void CDSPlayer::SetSubTitleDelay(float fValue)
{
  m_processInfo->GetVideoSettingsLocked().SetAudioDelay(fValue);
  if (CStreamsManager::Get())
    CStreamsManager::Get()->SetSubTitleDelay(fValue);
}

bool CDSPlayer::InitWindow(HWND &hWnd)
{
  m_hInstance = (HINSTANCE)GetModuleHandle(NULL);
  if (m_hInstance == NULL)
    CLog::Log(LOGDEBUG, "{} : GetModuleHandle failed with %d", __FUNCTION__, GetLastError());

  RESOLUTION_INFO res = CDisplaySettings::GetInstance().GetCurrentResolutionInfo();
  int nWidth = res.iWidth;
  int nHeight = res.iHeight;
  m_className = L"Kodi:DSPlayer";

  // Register the windows class
  WNDCLASS wndClass;

  wndClass.style = CS_HREDRAW | CS_VREDRAW | CS_NOCLOSE;
  wndClass.lpfnWndProc = WndProc;
  wndClass.cbClsExtra = 0;
  wndClass.cbWndExtra = 0;
  wndClass.hInstance = m_hInstance;
  wndClass.hIcon = NULL;
  wndClass.hCursor = NULL;
  wndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  wndClass.lpszMenuName = NULL;
  wndClass.lpszClassName = m_className.c_str();

  if (!RegisterClass(&wndClass))
  {
    CLog::Log(LOGERROR, "{} : RegisterClass failed with %d", __FUNCTION__, GetLastError());
    return false;
  }

  hWnd = CreateWindow(m_className.c_str(), m_className.c_str(),
    WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
    0, 0, nWidth, nHeight, 
    g_hWnd, NULL, m_hInstance, NULL);
  if (hWnd == NULL)
  {
    CLog::Log(LOGERROR, "{} : CreateWindow failed with %d", __FUNCTION__, GetLastError());
    return false;
  }

  if (hWnd)
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  return true;
}

void CDSPlayer::DeInitWindow()
{
  // remove ourself as user data to ensure we're not called anymore
  SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);

  // destroy the hidden window
  DestroyWindow(m_hWnd);

  // unregister the window class
  UnregisterClass(AToW(m_className).c_str(), m_hInstance);

  // reset the hWnd
  m_hWnd = NULL;
}

void CDSPlayer::SetDSWndVisible(bool bVisible)
{
  int cmd;
  bVisible ? cmd = SW_SHOW : cmd = SW_HIDE;
  ShowWindow(m_hWnd, cmd);
  UpdateWindow(m_hWnd);
}

void CDSPlayer::SetRenderOnDS(bool bRender)
{
  m_renderOnDs = bRender;

  if (bRender)
    SetDSWndVisible(bRender);
}

void CDSPlayer::VideoParamsChange()
{
  PostMessage(new CDSMsg(CDSMsg::PLAYER_AV_CHANGE), false);
}

LRESULT CALLBACK CDSPlayer::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_MOUSEMOVE:
  case WM_LBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_MBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MOUSEWHEEL:
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYUP:
  case WM_SYSKEYDOWN:
  case WM_APPCOMMAND:
  case WM_SETFOCUS:
    ::PostMessage(g_hWnd, uMsg, wParam, lParam);
    return(0);
  case WM_SIZE:
    SetWindowPos(hWnd, 0, 0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    PostMessage(new CDSMsgBool(CDSMsg::RESET_DEVICE, true), false);
    return(0);
  }
  return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

//CThread
void CDSPlayer::OnStartup()
{
  m_threadID = GetThreadId(CThread::GetCurrentNativeThreadId());
}

void CDSPlayer::OnExit()
{
  if (PlayerState == DSPLAYER_LOADING)
    PlayerState = DSPLAYER_ERROR;

  // In case of, set the ready event
  // Prevent a dead loop
  m_hReadyEvent.Set();
  m_bStop = true;
  m_threadID = 0;
  
  CServiceBroker::GetWinSystem()->UnregisterRenderLoop(this);
#if TODO//identify its not in xbmc core anymore
  if (m_PlayerOptions.identify == false)
  {
    if (!m_bEof || PlayerState == DSPLAYER_ERROR)
      m_callback.OnPlayBackStopped();
    else
      m_callback.OnPlayBackEnded();
  }

  m_PlayerOptions.identify = false;
#endif
}

void CDSPlayer::Process()
{
  HRESULT hr = E_FAIL;
  CLog::Log(LOGINFO, "{} - Creating DS Graph", __FUNCTION__);

  CServiceBroker::GetWinSystem()->RegisterRenderLoop(this);
  // Set the selected video renderer
  SetCurrentVideoRenderer(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_VIDEORENDERER));

  // Create DirectShow Graph
  hr = g_dsGraph->SetFile(m_currentFileItem, m_PlayerOptions);

  if (FAILED(hr))
  {
    CLog::Log(LOGERROR, "{} - Failed creating DS Graph", __FUNCTION__);
    PlayerState = DSPLAYER_ERROR;
    return;
  }

  m_pGraphThread.SetCurrentRate(1);
  m_pGraphThread.Create();
  UpdateApplication();

  g_dsSettings.pRendererSettings->bAllowFullscreen = m_PlayerOptions.fullscreen;
#if 0
  if (m_PlayerOptions.identify == false) m_callback.OnPlayBackStarted();
#endif

  // Start playback
  // If there's an error, the lock must be released in order to show the error dialog
  CLog::Log(LOGDEBUG, "{} - graph built, releasing the thread that opened the file", __FUNCTION__);

  // The splitter has finished scanning the stream and is demuxing it, so let the disc off the
  // hold that kept the start of the stream where the splitter could go back to it. Everything
  // below -- choosing streams, madVR bringing its device up, the retries that follow -- takes
  // seconds, and the splitter spends them reading the disc: held any longer it reaches the end
  // of the hold, is told the stream is short, and takes that for the end of the film.
  CDSBlurayStream::NoteGraphBuilt();

  m_hReadyEvent.Set();

  if (PlayerState != DSPLAYER_ERROR)
  {
    m_HasVideo = true;
    m_HasAudio = true;

    // Select Audio Stream, Delay
    CLog::Log(LOGDEBUG, "{} - choosing the audio stream", __FUNCTION__);
    if (CStreamsManager::Get()) CStreamsManager::Get()->SelectBestAudio();
    CLog::Log(LOGDEBUG, "{} - audio stream chosen", __FUNCTION__);
    SetAVDelay(CMediaSettings::GetInstance().GetDefaultVideoSettings().m_AudioDelay);

    // Select Subtitle Stream, Delay, On/Off
    
    CLog::Log(LOGDEBUG, "{} - choosing the subtitle stream", __FUNCTION__);
    if (CStreamsManager::Get()) CStreamsManager::Get()->SelectBestSubtitle(m_currentFileItem.GetPath());
    CLog::Log(LOGDEBUG, "{} - subtitle stream chosen", __FUNCTION__);
    SetSubTitleDelay(CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleDelay);
    SetSubtitleVisible(CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleOn);

    CMediaSettings::GetInstance().GetAtStartVideoSettings() = CMediaSettings::GetInstance().GetDefaultVideoSettings();

    if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SHOWBDTITLECHOICE))
      ShowEditionDlg(true);

    // Seek
    if (CDSBlurayNavigator::Get())
    {
      // A disc running its own navigation plays what it decides to play, from wherever it
      // has got to. There is no position to seek to, and asking for one stalls the graph
      // and everything waiting behind it, the interface included.
      CLog::Log(LOGDEBUG, "{} - the disc is navigating itself, starting where it is",
                __FUNCTION__);
    }
    else if (m_PlayerOptions.starttime > 0)
      g_dsGraph->Seek(SEC_TO_DS_TIME(m_PlayerOptions.starttime), 1U, false);
    else
      g_dsGraph->Seek(SEC_TO_DS_TIME(0), 1U, false);

    // Starts playback. Run() is asynchronous and a renderer that is still getting ready can
    // leave the graph cued rather than running, with nothing to try again — madVR brings its
    // device up a moment after the graph is built. Keep asking until the graph really runs,
    // or nothing will consume the stream: the picture freezes on whatever was pushed while
    // connecting, and a disc menu neither animates nor reacts.
    for (int attempt = 0; attempt < 20; ++attempt)
    {
      g_dsGraph->Play(true);

      // Waiting here without answering messages froze the interface for the whole of this
      // loop: the application thread was left waiting on the video window this thread owns,
      // which is what the startup race was
      SleepAnsweringMessages(250ms);
      g_dsGraph->UpdateState();

      if (PlayerState == DSPLAYER_PLAYING)
        break;

      CLog::Log(LOGDEBUG, "{} - the graph is not running yet (state {}), asking again",
                __FUNCTION__, static_cast<int>(PlayerState));
    }

    // Already done above for a disc that got this far, and here for anything that reached
    // playback another way
    CDSBlurayStream::NoteGraphRunning();

    if (CGraphFilters::Get()->IsDVD())
      CStreamsManager::Get()->LoadDVDStreams();
    
    m_callback.OnPlayBackStarted(m_currentFileItem);
  }

  while (!m_bStop && PlayerState != DSPLAYER_CLOSED && PlayerState != DSPLAYER_LOADING)
  {
    HandleMessages();
    //PostMessage(new CDSMsg(CDSMsg::PLAYER_PAUSE));
    

  }

}

void CDSPlayer::ApplyWhatTheDiscAsked()
{
  // Each of these is what a viewer just chose from the disc's own menu, so it is worth a
  // line in the log whether or not it turns out to be possible.

  const int64_t seekTo = m_discSeekWanted.exchange(-1);
  if (seekTo >= 0)
  {
    CLog::Log(LOGINFO, "{} - the disc's menu asked for {} ms into the programme", __FUNCTION__,
              seekTo);
    SeekTime(seekTo);
  }

  const int audio = m_discAudioWanted.exchange(-1);
  if (audio >= 0)
  {
    CLog::Log(LOGINFO, "{} - the disc's menu asked for audio stream {}", __FUNCTION__, audio);
    SetAudioStream(audio);
  }

  const int subtitle = m_discSubtitleWanted.exchange(-1);
  if (subtitle >= 0)
  {
    CLog::Log(LOGINFO, "{} - the disc's menu asked for subtitle stream {}", __FUNCTION__,
              subtitle);
    SetSubtitle(subtitle);
  }

  const int subtitlesVisible = m_discSubtitlesVisibleWanted.exchange(-1);
  if (subtitlesVisible >= 0)
  {
    CLog::Log(LOGINFO, "{} - the disc's menu asked for subtitles {}", __FUNCTION__,
              subtitlesVisible ? "on" : "off");
    SetSubtitleVisible(subtitlesVisible != 0);
  }
}

void CDSPlayer::RebuildGraphForDisc()
{
  CLog::Log(LOGINFO, "{} - the disc has moved to another programme, opening it again",
            __FUNCTION__);

  // Taking the graph apart and building another one by hand deadlocked against the graph's
  // own streaming threads. Going round through the player instead uses the path that opens
  // every other file, which is well travelled and already knows how to stop cleanly.
  //
  // The disc itself is deliberately left open across this, so it carries on from whatever it
  // moved to rather than starting over at first play, and a BD-J disc keeps everything its
  // menu was running.
  m_keepDiscOpen = true;

  // The disc may pass through another playlist or two before it settles on what it is really
  // going to play. Those are not separate requests to open it again.
  if (CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get())
    navigator->SuspendProgrammeChanges();

  // Close the whole thing from this thread, not just stop it. Taking the graph down
  // destroys the video renderer, and madVR only finishes going away once the application
  // thread has answered its render handshake — so the application thread cannot be the one
  // taking it down, or it waits for a reply only it could give. Leaving the close to the
  // application's ordinary open path put it exactly there.
  CDSBlurayStream::StopFeedingTheGraph();
  CloseFile();
  CLog::Log(LOGINFO, "{} - closed, asking for the new programme", __FUNCTION__);

  CFileItem* item = new CFileItem(m_currentFileItem);
  CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_PLAY, 0, 0, static_cast<void*>(item));
}

void CDSPlayer::SleepAnsweringMessages(std::chrono::milliseconds duration)
{
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(duration.count());

  for (;;)
  {
    const DWORD now = GetTickCount();
    if (now >= deadline)
      return;

    // Wake for a message as readily as for the end of the wait
    MsgWaitForMultipleObjects(0, nullptr, FALSE, deadline - now, QS_ALLINPUT);

    // Answer what was sent to this thread's windows. Messages posted to the thread itself
    // belong to HandleMessages, which knows how to complete and release them, so they are
    // taken out of the way and put back once the window messages have been dealt with.
    std::vector<MSG> ours;
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      if (msg.message == WM_QUIT)
      {
        PostQuitMessage(static_cast<int>(msg.wParam));
        break;
      }

      if (msg.hwnd == NULL)
      {
        ours.push_back(msg);
        continue;
      }

      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    for (const MSG& mine : ours)
      PostThreadMessage(::GetCurrentThreadId(), mine.message, mine.wParam, mine.lParam);
  }
}

void CDSPlayer::HandleMessages()
{
  //std::shared_ptr<CDs> pMsg = nullptr;
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) != 0)
  {
    if (msg.message != WM_GRAPHMESSAGE)
    {
      // This thread owns windows besides this loop's own mailbox: the video renderer makes
      // one on whichever thread builds the graph. Their messages have to be dispatched, or
      // anything that sends to those windows waits forever - stopping madVR does exactly
      // that, which left the graph impossible to stop.
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      continue;
    }

    if (msg.message == WM_GRAPHMESSAGE)
    {
      CDSMsg* pMsg = reinterpret_cast<CDSMsg *>(msg.lParam);
      CLog::Log(LOGDEBUG, "{} - received message {}, state {}", __FUNCTION__,
                static_cast<int>(pMsg->GetMessageType()), static_cast<int>(PlayerState));
      //CLog::Log(LOGDEBUG, "{} Message received : {} on thread 0x{}", __FUNCTION__, pMsg->GetMessageType(), m_threadID);

      if (CDSPlayer::PlayerState == DSPLAYER_CLOSED || CDSPlayer::PlayerState == DSPLAYER_LOADING)
      {
        pMsg->Set();
        pMsg->Release();
        break;
      }
      if (pMsg->IsType(CDSMsg::PLAYER_AV_CHANGE))
      {
        CServiceBroker::GetDataCacheCore().SignalAudioInfoChange();
        CServiceBroker::GetDataCacheCore().SignalVideoInfoChange();
        CServiceBroker::GetDataCacheCore().SignalSubtitleInfoChange();
        //m_callback.OnAVChange();
        /*IPlayerCallback* cb = &m_callback;
        m_outboundEvents->Submit([=]() {
          cb->OnAVChange();
          });*/
      }
      if (pMsg->IsType(CDSMsg::GENERAL_SET_WINDOW_POS))
      {
        g_dsGraph->UpdateWindowPosition();
      }
      if (pMsg->IsType(CDSMsg::RESET_DEVICE))
      {
        const auto appPlayer = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();        
        CDSMsgBool* speMsg = reinterpret_cast<CDSMsgBool *>(pMsg);
        appPlayer->Reset(speMsg->m_value);
      }
      if (pMsg->IsType(CDSMsg::SET_WINDOW_POS))
      {
        SetPosition();
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_SEEK_TIME))
      {
        CDSMsgPlayerSeekTime* speMsg = reinterpret_cast<CDSMsgPlayerSeekTime *>(pMsg);
        g_dsGraph->Seek(speMsg->GetTime(), speMsg->GetFlags(), speMsg->ShowPopup());
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_SEEK))
      {
        CDSMsgPlayerSeek* speMsg = reinterpret_cast<CDSMsgPlayerSeek*>(pMsg);
        g_dsGraph->Seek(speMsg->Forward(), speMsg->LargeStep());
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_SEEK_PERCENT))
      {
        CDSMsgDouble * speMsg = reinterpret_cast<CDSMsgDouble *>(pMsg);
        g_dsGraph->SeekPercentage((float)speMsg->m_value);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_PAUSE))
      {
        g_dsGraph->Pause();
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_STOP))
      {
        //save state of file
        CFileItem fileItem(m_currentFileItem);
        CBookmark bookmark;


          bookmark.totalTimeInSeconds = DS_TIME_TO_SEC(g_dsGraph->GetTotalTime());
          bookmark.timeInSeconds = DS_TIME_TO_SEC(g_dsGraph->GetTime());
        
        bookmark.player = m_name;
        bookmark.playerState = GetPlayerState();
        m_outboundEvents->Submit([=]() {
          m_callback.OnPlayerCloseFile(fileItem, bookmark);
          });
        CDSMsgBool* speMsg = reinterpret_cast<CDSMsgBool*>(pMsg);
        g_dsGraph->Stop(speMsg->m_value);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_PLAY))
      {
        CDSMsgBool* speMsg = reinterpret_cast<CDSMsgBool *>(pMsg);
        g_dsGraph->Play(speMsg->m_value);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_CLOSE_GRAPH))
      {
        // This thread built the graph, so this thread owns the video renderer's window and
        // whatever apartment it lives in. Releasing madVR anywhere else never comes back.
        CLog::Log(LOGDEBUG, "{} - taking the graph apart on the thread that built it",
                  __FUNCTION__);
        g_dsGraph->CloseFile();
        CLog::Log(LOGDEBUG, "{} - the graph is gone", __FUNCTION__);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_UPDATE_TIME))
      {
        g_dsGraph->UpdateTime();
        m_processInfo->SetPlayTimes(0, g_dsGraph->GetTime(), 0, g_dsGraph->GetTotalTime());
        
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_PLAYBACK_STARTED))
      {
        CServiceBroker::GetAppMessenger()->PostMsg(TMSG_SWITCHTOFULLSCREEN);
        m_callback.OnAVStarted(m_currentFileItem);
      }

      /*DVD COMMANDS*/
      // Everything below drives a DVD through DirectShow's DVD Navigator, which only exists
      // when a DVD is playing. A Blu-ray navigated by us has none of it, and the handlers
      // reach straight through those pointers - one mouse movement over the window was
      // enough to take this thread down, and with it every message that would ever be sent
      // to it, which is what made the player stop answering after entering a disc menu.
      const bool haveDvdNavigator =
          CGraphFilters::Get()->DVD.dvdInfo && CGraphFilters::Get()->DVD.dvdControl;

      // The two mouse messages are not only for DVDs: they are also how the pointer reaches
      // the interface at all while a video window is up, which is what lets the viewer move
      // the mouse and click on the OSD. They are handled below whatever is playing, and only
      // their DVD button lookup is skipped.
      if (!haveDvdNavigator && pMsg->GetMessageType() > CDSMsg::PLAYER_DVD_MOUSE_CLICK &&
          pMsg->GetMessageType() <= CDSMsg::PLAYER_DVD_MENU_ANGLE)
      {
        CLog::Log(LOGDEBUG, "{} - ignoring a DVD command ({}), this disc is not a DVD",
                  __FUNCTION__, static_cast<int>(pMsg->GetMessageType()));
        pMsg->Set();
        pMsg->Release();
        continue;
      }

      if (pMsg->IsType(CDSMsg::PLAYER_DVD_MOUSE_MOVE))
      {
        CDSMsgInt* speMsg = reinterpret_cast<CDSMsgInt *>(pMsg);
        //TODO make the xbmc gui stay hidden when moving mouse over menu
        POINT pt;
        pt.x = GET_X_LPARAM(speMsg->m_value);
        pt.y = GET_Y_LPARAM(speMsg->m_value);
        ULONG pButtonIndex;
        /**** Didnt found really where VideoPlayer are doing it exactly so here it is *****/
        XBMC_Event newEvent;
        newEvent.type = XBMC_MOUSEMOTION;
        newEvent.motion.x = (uint16_t)pt.x;
        newEvent.motion.y = (uint16_t)pt.y;
        CServiceBroker::GetAppPort()->OnEvent(
          newEvent);
        /*CGUIMessage pMsg(GUI_MSG_VIDEO_MENU_STARTED, 0, 0);
        g_windowManager.SendMessage(pMsg);*/
        /**** End of ugly hack ***/

        // Only a DVD has buttons to look up here; a Blu-ray has no DVD navigator at all, and
        // reaching through those pointers is what used to kill this thread
        if (haveDvdNavigator &&
            SUCCEEDED(CGraphFilters::Get()->DVD.dvdInfo->GetButtonAtPosition(pt, &pButtonIndex)))
          CGraphFilters::Get()->DVD.dvdControl->SelectButton(pButtonIndex);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MOUSE_CLICK))
      {
        CDSMsgInt* speMsg = reinterpret_cast<CDSMsgInt *>(pMsg);
        POINT pt;
        pt.x = GET_X_LPARAM(speMsg->m_value);
        pt.y = GET_Y_LPARAM(speMsg->m_value);
        ULONG pButtonIndex;

        // A click has to reach the interface the same way a movement does, or the OSD can be
        // pointed at but never pressed
        XBMC_Event newEvent;
        newEvent.type = XBMC_MOUSEBUTTONDOWN;
        newEvent.button.button = XBMC_BUTTON_LEFT;
        newEvent.button.x = (uint16_t)pt.x;
        newEvent.button.y = (uint16_t)pt.y;
        CServiceBroker::GetAppPort()->OnEvent(newEvent);

        newEvent.type = XBMC_MOUSEBUTTONUP;
        CServiceBroker::GetAppPort()->OnEvent(newEvent);

        if (haveDvdNavigator &&
            SUCCEEDED(CGraphFilters::Get()->DVD.dvdInfo->GetButtonAtPosition(pt, &pButtonIndex)))
          CGraphFilters::Get()->DVD.dvdControl->SelectAndActivateButton(pButtonIndex);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_UP))
      {
        CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Upper);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_DOWN))
      {
        CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Lower);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_LEFT))
      {
        CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Left);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_NAV_RIGHT))
      {
        CGraphFilters::Get()->DVD.dvdControl->SelectRelativeButton(DVD_Relative_Right);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_ROOT))
      {
        // send a message to everyone that we've gone to the menu
        CGUIMessage msg(GUI_MSG_VIDEO_MENU_STARTED, 0, 0);
        CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
        CGraphFilters::Get()->DVD.dvdControl->ShowMenu(DVD_MENU_Root, DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_EXIT))
      {
        CGraphFilters::Get()->DVD.dvdControl->Resume(DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_BACK))
      {
        CGraphFilters::Get()->DVD.dvdControl->ReturnFromSubmenu(DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_SELECT))
      {
        CGraphFilters::Get()->DVD.dvdControl->ActivateButton();
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_TITLE))
      {
        CGraphFilters::Get()->DVD.dvdControl->ShowMenu(DVD_MENU_Title, DVD_CMD_FLAG_Block | DVD_CMD_FLAG_Flush, NULL);
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_SUBTITLE))
      {
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_AUDIO))
      {
      }
      else if (pMsg->IsType(CDSMsg::PLAYER_DVD_MENU_ANGLE))
      {
      }
      pMsg->Set();
      pMsg->Release();
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

void CDSPlayer::Pause()
{  
  if (PlayerState == DSPLAYER_CLOSING)
    return;

  if (PlayerState == DSPLAYER_LOADING || PlayerState == DSPLAYER_LOADED )
  {
    g_dsGraph->QueuePause();
    return;
  }

  g_dsGraph->UpdateState();

  m_pGraphThread.SetSpeedChanged(true);
  if (PlayerState == DSPLAYER_PAUSED)
  {
    m_pGraphThread.SetCurrentRate(1);
    m_callback.OnPlayBackResumed();
  }
  else
  {
    m_pGraphThread.SetCurrentRate(0);
    m_callback.OnPlayBackPaused();
  }
  PostMessage(new CDSMsg(CDSMsg::PLAYER_PAUSE));
}
void CDSPlayer::SetSpeed(float iSpeed)
{
#if TODO
  if (iSpeed != 1)
    g_infoManager.SetDisplayAfterSeek();
#endif
  m_pGraphThread.SetCurrentRate(iSpeed);
  m_pGraphThread.SetSpeedChanged(true);
}

float CDSPlayer::GetSpeed()
{
  return m_pGraphThread.GetCurrentRate();
}

bool CDSPlayer::SupportsTempo()
{
  return m_canTempo;
}

void CDSPlayer::Seek(bool bPlus, bool bLargeStep, bool bChapterOverride)
{
  PostMessage(new CDSMsgPlayerSeek(bPlus, bLargeStep));
}

void CDSPlayer::SeekPercentage(float iPercent)
{
  PostMessage(new CDSMsgDouble(CDSMsg::PLAYER_SEEK_PERCENT, iPercent));
}

bool CDSPlayer::HasMenu() const
{
  return CDSBlurayNavigator::Get() != nullptr || g_dsGraph->IsDvd();
}

void CDSPlayer::ToggleDiscInput()
{
  const bool toTheDisc = !IsInMenu();
  m_discInputForced = toTheDisc ? 1 : 0;

  CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get();
  CLog::Log(LOGINFO,
            "{} - the viewer has sent the keyboard to {}. What we could see was {}, and the "
            "disc said its menu was {}.",
            __FUNCTION__, toTheDisc ? "the disc" : "Kodi",
            navigator && navigator->MenuOnScreen() ? "a menu" : "no menu",
            navigator && navigator->DiscSaysMenuVisible() ? "visible" : "not visible");
}

void CDSPlayer::ForgetDiscInputOverride()
{
  if (m_discInputForced.exchange(-1) != -1)
  {
    CLog::Log(LOGDEBUG, "{} - the disc has changed what is on screen, so where the keyboard "
                        "goes is its business again", __FUNCTION__);
  }
}

bool CDSPlayer::IsInMenu() const
{
  // The viewer has overruled us, and until the disc changes what is on screen they are right
  // by definition: this exists precisely for the times the answer below is wrong.
  const int forced = m_discInputForced;
  if (forced >= 0)
    return forced != 0;

  CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get();
  if (navigator)
  {
    // Whether a menu is up is whether the disc is drawing one, not whether its menu code is
    // loaded. libbluray answers the latter, and on a BD-J disc that stays true for the whole
    // film, which sent every key press to the disc and left Kodi's own OSD unreachable.
    // Asking what is drawn is also what makes a popup menu over a playing film work: it is
    // a menu exactly while its graphics are there.
    return navigator->MenuOnScreen();
  }

  return g_dsGraph->IsInMenu();
}

MenuType CDSPlayer::GetSupportedMenuType() const
{
  // A navigated disc presents its own menus. Saying so is what makes the application route
  // navigation keys to the player instead of treating them as seeks.
  return CDSBlurayNavigator::Get() ? MenuType::NATIVE : MenuType::NONE;
}

bool CDSPlayer::OnAction(const CAction &action)
{
  if (CDSBlurayNavigator* navigator = CDSBlurayNavigator::Get())
  {
    // Answered before anything looks at IsInMenu(), because being unable to reach this while
    // the player believes a menu is up would defeat the whole point of it
    if (action.GetID() == ACTION_DSPLAYER_TOGGLE_DISC_INPUT)
    {
      ToggleDiscInput();
      return true;
    }

    if (action.GetID() == ACTION_SHOW_VIDEOMENU)
      return navigator->ShowMenu();

    // The key that opens the OSD asks the disc for its menu instead, while the disc has no
    // menu up. Someone watching a Blu-ray who presses it wants the disc's popup -- that is
    // what the button does on a player and on a remote -- and until the disc drew a menu of
    // its own there was no way to ask for one from the keyboard at all.
    //
    // Only while nothing is up. Once the popup is on screen the same key has to reach Kodi's
    // OSD again, so it is left alone and the window handles it as it always did; the two
    // therefore alternate, and neither can be locked out by the other. If the disc has no
    // popup to give, the key is not swallowed and the OSD opens as usual.
    if (action.GetID() == ACTION_SHOW_OSD && !IsInMenu())
    {
      if (navigator->ShowMenu())
        return true;

      CLog::Log(LOGDEBUG, "{} - the disc had no menu to show, leaving the key for the OSD",
                __FUNCTION__);
    }

    // Everything else only belongs to the disc while a menu is up, otherwise the arrow keys
    // would stop seeking during normal playback. IsInMenu() rather than the navigator's own
    // answer, because the disc keeps its menu up underneath a film played from a playlist.
    if (IsInMenu())
    {
      switch (action.GetID())
      {
        case ACTION_PREVIOUS_MENU:
        case ACTION_NAV_BACK:
          navigator->OnBack();
          return true;
        case ACTION_MOVE_LEFT:
          navigator->OnLeft();
          return true;
        case ACTION_MOVE_RIGHT:
          navigator->OnRight();
          return true;
        case ACTION_MOVE_UP:
          navigator->OnUp();
          return true;
        case ACTION_MOVE_DOWN:
          navigator->OnDown();
          return true;
        case ACTION_SELECT_ITEM:
          navigator->OnSelect();
          return true;
        default:
          break;
      }
    }
  }

#if TODO
  if (g_dsGraph->IsDvd())
  {
    if (action.GetID() == ACTION_SHOW_VIDEOMENU)
    {
      PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_ROOT), false);
      return true;
    }
    if (g_dsGraph->IsInMenu())
    {
      switch (action.GetID())
      {
      case ACTION_PREVIOUS_MENU:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_BACK), false);
        break;
      case ACTION_MOVE_LEFT:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_LEFT), false);
        break;
      case ACTION_MOVE_RIGHT:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_RIGHT), false);
        break;
      case ACTION_MOVE_UP:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_UP), false);
        break;
      case ACTION_MOVE_DOWN:
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_NAV_DOWN), false);
        break;
        /*case ACTION_MOUSE_MOVE:
        case ACTION_MOUSE_LEFT_CLICK:
        {
        CRect rs, rd;
        GetVideoRect(rs, rd);
        CPoint pt(action.GetAmount(), action.GetAmount(1));
        if (!rd.PtInRect(pt))
        return false;
        pt -= CPoint(rd.x1, rd.y1);
        pt.x *= rs.Width() / rd.Width();
        pt.y *= rs.Height() / rd.Height();
        pt += CPoint(rs.x1, rs.y1);
        if (action.GetID() == ACTION_MOUSE_LEFT_CLICK)
        SendMessage(g_hWnd, WM_COMMAND, ID_DVD_MOUSE_CLICK,MAKELPARAM(pt.x,pt.y));
        else
        SendMessage(g_hWnd, WM_COMMAND, ID_DVD_MOUSE_MOVE,MAKELPARAM(pt.x,pt.y));
        return true;
        }
        break;*/
      case ACTION_SELECT_ITEM:
      {
        // show button pushed overlay
        PostMessage(new CDSMsg(CDSMsg::PLAYER_DVD_MENU_SELECT), false);
      }
        break;
      case REMOTE_0:
      case REMOTE_1:
      case REMOTE_2:
      case REMOTE_3:
      case REMOTE_4:
      case REMOTE_5:
      case REMOTE_6:
      case REMOTE_7:
      case REMOTE_8:
      case REMOTE_9:
      {
        // Offset from key codes back to button number
        // int button = action.actionId - REMOTE_0;
        //CLog::Log(LOGDEBUG, " - button pressed %d", button);
        //pStream->SelectButton(button);
      }
        break;
      default:
        return false;
        break;
      }
      return true; // message is handled
    }
  }

  if (g_pPVRStream)
  {
    switch (action.GetID())
    {
    case ACTION_MOVE_UP:
    case ACTION_NEXT_ITEM:
    case ACTION_CHANNEL_UP:
      if (SelectChannel(true))
      {
        g_infoManager.SetDisplayAfterSeek();
        ShowPVRChannelInfo();
      }
      else if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      {
        m_callback.OnPlayBackStopped();
      }
      else
      {
        CLog::Log(LOGWARNING, "{} - failed to switch channel. playback stopped", __FUNCTION__);
        CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_STOP);
      }
      return true;
      break;

    case ACTION_MOVE_DOWN:
    case ACTION_PREV_ITEM:
    case ACTION_CHANNEL_DOWN:
      if (SelectChannel(false))
      {
        g_infoManager.SetDisplayAfterSeek();
        ShowPVRChannelInfo();
      }
      else if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      {
        m_callback.OnPlayBackStopped();
      }
      else
      {
        CLog::Log(LOGWARNING, "{} - failed to switch channel. playback stopped", __FUNCTION__);
        CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_STOP);
      }
      return true;
      break;

    case ACTION_CHANNEL_SWITCH:
      // Offset from key codes back to button number
      int channel = (int)action.GetAmount();
      if (SwitchChannel(channel))
      {
        g_infoManager.SetDisplayAfterSeek();
        ShowPVRChannelInfo();
      }
      else if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      {
        m_callback.OnPlayBackStopped();
      }
      else
      {
        CLog::Log(LOGWARNING, "{} - failed to switch channel. playback stopped", __FUNCTION__);
        CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_STOP);
      }
      return true;
      break;
    }
  }

  switch (action.GetID())
  {
  case ACTION_NEXT_ITEM:
  case ACTION_PAGE_UP:
    if (GetChapterCount() > 0)
    {
      SeekChapter(GetChapter() + 1);
      g_infoManager.SetDisplayAfterSeek();
      return true;
    }
    else
      break;
  case ACTION_PREV_ITEM:
  case ACTION_PAGE_DOWN:
    if (GetChapterCount() > 0)
    {
      SeekChapter(GetChapter() - 1);
      g_infoManager.SetDisplayAfterSeek();
      return true;
    }
    else
      break;
  case ACTION_PLAYER_DEBUG:
    m_renderManager.ToggleDebug();
    break;

  case ACTION_PLAYER_PROCESS_INFO:
    if (g_windowManager.GetActiveWindow() != WINDOW_DIALOG_DSPLAYER_PROCESS_INFO)
    {
      g_windowManager.ActivateWindow(WINDOW_DIALOG_DSPLAYER_PROCESS_INFO);
      return true;
    }
    break;  
  case ACTION_DSPLAYER_USERSETTINGS_1:
    LoadMadvrSettings(1);
    break;
  case ACTION_DSPLAYER_USERSETTINGS_2:
    LoadMadvrSettings(2);
    break;
  case ACTION_DSPLAYER_USERSETTINGS_3:
    LoadMadvrSettings(3);
    break;
  case ACTION_DSPLAYER_USERSETTINGS_SD:
    LoadMadvrSettings(MADVR_RES_SD);
    break;
  case ACTION_DSPLAYER_USERSETTINGS_720:
    LoadMadvrSettings(MADVR_RES_720);
    break;
  case ACTION_DSPLAYER_USERSETTINGS_1080:
    LoadMadvrSettings(MADVR_RES_1080);
    break;
  case ACTION_DSPLAYER_USERSETTINGS_2160:
    LoadMadvrSettings(MADVR_RES_2160);
    break;
  case ACTION_DSPLAYER_USERSETTINGS_ATSTART:
    LoadMadvrSettings(0);
    break;
  }
#endif
  // return false to inform the caller we didn't handle the message
  return false;
}

void CDSPlayer::LoadMadvrSettings(int id)
{
  if (id < 0 || !m_isMadvr || CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_MANAGEMADVRWITHKODI) != KODIGUI_LOAD_DSPLAYER)
    return;

  CMadvrSettings &madvrSettings = CMediaSettings::GetInstance().GetCurrentMadvrSettings();

  if (id != 0)
  {
    CDSPlayerDatabase dspdb;
    if (!dspdb.Open())
      return;
    std::string sId;

    if (id < MADVR_RES_SD)
    {
      dspdb.GetUserSettings(id, madvrSettings);
      sId = StringUtils::Format("User settings #{}", id);
    }
    else
    {
      dspdb.GetResSettings(id, madvrSettings);
      sId = StringUtils::Format("Resolution settings {}p", id);
      if (id == MADVR_RES_SD)
        sId = StringUtils::Format("Resolution settings SD", id);
    }

    g_application.GetComponent<CApplicationPlayer>()->RestoreSettings();
    dspdb.Close();
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "DSPlayer", sId, TOAST_MESSAGE_TIME, false);
  }
  else
  {
    madvrSettings.RestoreAtStartSettings();
    g_application.GetComponent<CApplicationPlayer>()->RestoreSettings();

    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "DSPlayer", "Restored original video settings", TOAST_MESSAGE_TIME, false);
  }
}

// Time is in millisecond
void CDSPlayer::SeekTime(__int64 iTime)
{
  int seekOffset = (int)(iTime - DS_TIME_TO_MSEC(g_dsGraph->GetTime(true)));
  PostMessage(new CDSMsgPlayerSeekTime(MSEC_TO_DS_TIME((iTime < 0) ? 0 : iTime)));
  m_callback.OnPlayBackSeek((int)iTime, seekOffset);
}

// Time is in millisecond
bool CDSPlayer::SeekTimeRelative(__int64 iTime)
{
  __int64 abstime = (__int64)(DS_TIME_TO_MSEC(g_dsGraph->GetTime(true)) + iTime);
  PostMessage(new CDSMsgPlayerSeekTime(MSEC_TO_DS_TIME((abstime < 0) ? 0 : abstime)));
  m_callback.OnPlayBackSeek((int)abstime, (int)iTime);
  return true;
}

__int64 CDSPlayer::GetTime()
{
   std::unique_lock<CCriticalSection> lock(m_StateSection); 
   __int64 tmm;
   tmm = llrint(DS_TIME_TO_MSEC(g_dsGraph->GetTime()));
  return tmm;
}

void CDSPlayer::UpdateApplication()
{
#if TODO
  if (g_pPVRStream)
  {
    CFileItem item(g_application.CurrentFileItem());
    if (g_pPVRStream->UpdateItem(item))
    {
      g_application.CurrentFileItem() = item;
      CServiceBroker::GetAppMessenger()->PostMsg(TMSG_UPDATE_CURRENT_ITEM, 0, -1, static_cast<void*>(new CFileItem(item)));
    }
  }
#endif
}

// Called after fast channel switch.
void CDSPlayer::UpdateChannelSwitchSettings()
{
  if (g_dsGraph->GetTime(true) + MSEC_TO_DS_TIME(3000) < g_dsGraph->GetTotalTime(true))
  {
    // Pause playback
    PostMessage(new CDSMsgBool(CDSMsg::PLAYER_PAUSE, true), false);
    // Seek to the end of timeshift buffer
    PostMessage(new CDSMsgPlayerSeekTime(g_dsGraph->GetTotalTime(true) - MSEC_TO_DS_TIME(3000)));
    // Start playback
    PostMessage(new CDSMsgBool(CDSMsg::PLAYER_PLAY, true), false);
  }

#ifdef HAS_VIDEO_PLAYBACK
  // when using fast channel switching some shortcuts are taken which 
  // means we'll have to update the view mode manually
  m_renderManager.SetViewMode(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_ViewMode);
#endif
}

bool CDSPlayer::SwitchChannel(unsigned int iChannelNumber)
{
#if TODO
  m_PlayerOptions.identify = true;

  bool bResult = g_pPVRStream->SelectChannelByNumber(iChannelNumber);
  
  m_PlayerOptions.identify = false;

  return bResult;
#endif
  return false;
}

#if TODO
bool CDSPlayer::SwitchChannel(const CPVRChannelPtr &channel)
{
  if (!g_PVRManager.CheckParentalLock(channel))
    return false;

  /* set GUI info */
  if (!g_PVRManager.PerformChannelSwitch(channel, true))
    return false;

  m_PlayerOptions.identify = true;

  bool bResult = g_pPVRStream->SelectChannel(channel);

  m_PlayerOptions.identify = false;

  return bResult;
}
#endif

bool CDSPlayer::SelectChannel(bool bNext)
{
#if TODO
  m_PlayerOptions.identify = true;

  bool bShowPreview = false;/*(CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("pvrplayback.channelentrytimeout") > 0);*/ // TODO

  if (!bShowPreview)
  {
    g_infoManager.SetDisplayAfterSeek(100000);
  }

  bool bResult = (bNext ? g_pPVRStream->NextChannel(bShowPreview) : g_pPVRStream->PrevChannel(bShowPreview));
  
  m_PlayerOptions.identify = false;

  return bResult;
#endif
  return false;
}

bool CDSPlayer::ShowPVRChannelInfo()
{
#if TODO
  bool bReturn(false);

  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_PVRMENU_DISPLAYCHANNELINFO) > 0)
  {
    g_PVRManager.ShowPlayerInfo(CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_PVRMENU_DISPLAYCHANNELINFO));

    bReturn = true;
  }

  return bReturn;
#endif
  return false;
}

void CDSPlayer::FrameMove()
{
  m_renderManager.FrameMove();

  // Asking the graph the time goes down to the splitter and, through it, to the source. It
  // is done every frame on the application thread, so anything slow in that chain stops the
  // interface. Traced sparsely to show whether that is what happened.
  static uint64_t times = 0;
  const bool trace = ((times++ % 200) == 0);
  if (trace)
    CLog::Log(LOGDEBUG, "{} - asking the graph for the time", __FUNCTION__);

  const double played = g_dsGraph->GetTime();
  const double total = g_dsGraph->GetTotalTime();

  if (trace)
    CLog::Log(LOGDEBUG, "{} - the graph says {} of {}", __FUNCTION__, played, total);

  m_processInfo->SetPlayTimes(0, DS_TIME_TO_MSEC(played), 0, DS_TIME_TO_MSEC(total));
}

void CDSPlayer::Render(bool clear, uint32_t alpha, bool gui)
{
  m_renderManager.Render(clear, 0, alpha, gui);
}

void CDSPlayer::FlushRenderer()
{
  m_renderManager.Flush();
}

void CDSPlayer::SetRenderViewMode(int mode, float zoom, float par, float shift, bool stretch)
{
  m_processInfo->GetVideoSettingsLocked().SetViewMode(mode, zoom, par, shift, stretch);
  m_renderManager.SetVideoSettings(m_processInfo->GetVideoSettings());
  m_renderManager.SetViewMode(mode);
  if (UsingDS(DIRECTSHOW_RENDERER_MADVR))
  {
    m_pAllocatorCallback->SetViewMode(mode);
  }

}

float CDSPlayer::GetRenderAspectRatio() const
{
  return m_renderManager.GetAspectRatio();
}

void CDSPlayer::TriggerUpdateResolution()
{
  m_renderManager.TriggerUpdateResolution(0, 0, 0);
}

bool CDSPlayer::IsRenderingVideo() const
{
  return m_renderManager.IsConfigured();
}

bool CDSPlayer::IsRenderingGuiLayer()
{
  return m_renderManager.IsGuiLayer();
}

bool CDSPlayer::IsRenderingVideoLayer()
{
  return m_renderManager.IsVideoLayer();
}

bool CDSPlayer::Supports(EINTERLACEMETHOD method) const
{
  if (method == VS_INTERLACEMETHOD_NONE
    || method == VS_INTERLACEMETHOD_AUTO
    || method == VS_INTERLACEMETHOD_DEINTERLACE)
    return true;

  return false;
}

bool CDSPlayer::Supports(ESCALINGMETHOD method) const
{
  return m_renderManager.Supports(method);
}

bool CDSPlayer::Supports(ERENDERFEATURE feature) const
{
  return m_renderManager.Supports(feature);
}

void CDSPlayer::OnLostDisplay()
{
}

void CDSPlayer::OnResetDisplay()
{
}

CVideoSettings CDSPlayer::GetVideoSettings() const
{
  return m_processInfo->GetVideoSettings();
}

void CDSPlayer::SetVideoSettings(CVideoSettings& settings)
{
  m_processInfo->SetVideoSettings(settings);
  m_renderManager.SetVideoSettings(settings);
  //TODO add subtitle activation and delay
}

void CDSPlayer::UpdateProcessInfo(int index)
{
//TODO HAD TIMING FOR THE GUI
  if (index == CURRENT_STREAM)
    index = g_application.GetComponent<CApplicationPlayer>()->GetAudioStream();

  std::string info;

  //Renderers in dsplayer
  info = StringUtils::Format("{}, {}", CGraphFilters::Get()->VideoRenderer.osdname.c_str(), CGraphFilters::Get()->AudioRenderer.osdname.c_str());
  m_processInfo->SetVideoPixelFormat(info);
  //filters in dsplayer
  //this make mpcvr loop endlessly
  //m_processInfo->SetVideoDeintMethod(g_dsGraph->GetGeneralInfo());

  //AUDIO

  //add visible track number
  info = StringUtils::Format("{} {}", CStreamsManager::Get() ? CStreamsManager::Get()->GetChannels(index) : 0, g_localizeStrings.Get(14301).c_str());
  if (GetAudioStreamCount() > 1)
    info = StringUtils::Format("({}/{}) {} ", index + 1, GetAudioStreamCount(), info.c_str());
  m_processInfo->SetAudioChannels(info);

  m_processInfo->SetAudioBitsPerSample(CStreamsManager::Get() ? CStreamsManager::Get()->GetBitsPerSample(index) : 0);
  m_processInfo->SetAudioSampleRate(CStreamsManager::Get() ? CStreamsManager::Get()->GetSampleRate(index) : 0);

  //add delay info to audio decoder name;
  SetAudioCodeDelayInfo(index);

  //VIDEO
  unsigned int width = GetPictureWidth();
  unsigned int heigth = GetPictureHeight();
  m_processInfo->SetVideoDimensions(width, heigth);

  info = CStreamsManager::Get() ? CStreamsManager::Get()->GetVideoCodecName() : "";

  // add active decoder info
  std::pair<std::string, bool> activeDecoder;
  CGraphFilters::Get()->GetActiveDecoder(activeDecoder);

  if (!activeDecoder.first.empty())
    info += " (" + activeDecoder.first + ")";

  m_processInfo->SetVideoDecoderName(info, activeDecoder.second);
  m_processInfo->SetVideoDAR((float)width / (float)heigth);
  m_processInfo->SetVideoFps(m_fps);

  CServiceBroker::GetDataCacheCore().SignalAudioInfoChange();
  CServiceBroker::GetDataCacheCore().SignalVideoInfoChange();
}

void CDSPlayer::SetAudioCodeDelayInfo(int index)
{
  if (index == CURRENT_STREAM)
    index = g_application.GetComponent<CApplicationPlayer>()->GetAudioStream();

  std::string info;
  int iAudioDelay = CStreamsManager::Get() ? CStreamsManager::Get()->GetLastAVDelay() : 0;
  info = StringUtils::Format("{}, {}ms delay", CStreamsManager::Get() ? CStreamsManager::Get()->GetAudioCodecDisplayName(index).c_str() : "", iAudioDelay);

  m_processInfo->SetAudioDecoderName(info);

  CServiceBroker::GetDataCacheCore().SignalAudioInfoChange();
}

void CDSPlayer::DisplayChange(bool bExternalChange)
{
  m_renderManager.DisplayChange(bExternalChange);

  if (UsingDS())
    m_pAllocatorCallback->DisplayChange(bExternalChange);
}

int CDSPlayer::VideoDimsToResolution(int iWidth, int iHeight)
{
  //called fast for size
  int res = 0;
  int madvr_res = -1;

  if (iWidth == 0 || iHeight == 0)
    res = 0;
  else if (iWidth <= 720 && iHeight <= 480)
    res = 480;
  // 720x576 (PAL) (768 when rescaled for square pixels)
  else if (iWidth <= 768 && iHeight <= 576)
    res = 576;
  // 960x540 (sometimes 544 which is multiple of 16)
  else if (iWidth <= 960 && iHeight <= 544)
    res = 540;
  // 1280x720
  else if (iWidth <= 1280 && iHeight <= 720)
    res = 720;
  // 1920x1080
  else if (iWidth <= 1920 && iHeight <= 1080)
    res = 1080;
  // 4K
  else if (iWidth * iHeight >= 6000000)
    res = 2160;
  else
    res = 0;

  if (res == 480 || res == 540 || res == 576)
    madvr_res = MADVR_RES_SD;

  if (res == 720)
    madvr_res = MADVR_RES_720;

  if (res == 1080)
    madvr_res = MADVR_RES_1080;

  if (res == 2160)
    madvr_res = MADVR_RES_2160;

  return madvr_res;
}

void CDSPlayer::SetVisibleScreenArea(CRect activeVideoRect)
{
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_OSDINTOACTIVEAREA)
    && CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_COPYACTIVERECT)
    && activeVideoRect != CRect {0,0,0,0})
  {
    CRect wndRect = CServiceBroker::GetWinSystem()->GetGfxContext().GetViewWindow();

    CServiceBroker::GetSettingsComponent()->GetSettings()->SetInt(CSettings::SETTING_DSPLAYER_DSAREALEFT, activeVideoRect.x1 - wndRect.x1);
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetInt(CSettings::SETTING_DSPLAYER_DSAREARIGHT, wndRect.x2 - activeVideoRect.x2);
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetInt(CSettings::SETTING_DSPLAYER_DSAREATOP, activeVideoRect.y1 - wndRect.y1);
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetInt(CSettings::SETTING_DSPLAYER_DSAREABOTTOM, wndRect.y2 - activeVideoRect.y2);
  }
}

// IDSRendererAllocatorCallback
CRect CDSPlayer::GetActiveVideoRect()
{
  CRect activeVideoRect(0, 0, 0, 0);

  if (ReadyDS())
    activeVideoRect = m_pAllocatorCallback->GetActiveVideoRect();

  return activeVideoRect;
}

bool CDSPlayer::IsEnteringExclusive()
{
  if (UsingDS(DIRECTSHOW_RENDERER_MADVR))
    return m_pAllocatorCallback->IsEnteringExclusive();

  return false;
}

void CDSPlayer::EnableExclusive(bool bEnable)
{
  if (UsingDS(DIRECTSHOW_RENDERER_MADVR))
    m_pAllocatorCallback->EnableExclusive(bEnable);
}



void CDSPlayer::SetPixelShader() const
{
  if (UsingDS(DIRECTSHOW_RENDERER_MADVR) || UsingDS(DIRECTSHOW_RENDERER_MPCVR))
    m_pAllocatorCallback->SetPixelShader();
}

void CDSPlayer::SetResolution() const
{
  //TODO mpcvr set resolution
  if (UsingDS(DIRECTSHOW_RENDERER_MADVR) || UsingDS(DIRECTSHOW_RENDERER_MPCVR))
    m_pAllocatorCallback->SetResolution();
}

void CDSPlayer::SetPosition(CRect sourceRect, CRect videoRect, CRect viewRect)
{
  //this is called every time the window is resized
  m_sourceRect = sourceRect;
  m_videoRect = videoRect;
  m_viewRect = viewRect;

  if (!CDSPlayer::IsCurrentThread())
  {
    CDSPlayer::PostMessage(new CDSMsg(CDSMsg::GENERAL_SET_WINDOW_POS), false);
    return;
  }
  else
    SetPosition();
}

void CDSPlayer::SetPosition() const
{
  if (UsingDS())
    m_pAllocatorCallback->SetPosition(m_sourceRect, m_videoRect, m_viewRect);
}

bool CDSPlayer::ParentWindowProc(HWND hWnd, UINT uMsg, WPARAM *wParam, LPARAM *lParam, LRESULT *ret) const
{
  if (UsingDS(DIRECTSHOW_RENDERER_MADVR))
    return m_pAllocatorCallback->ParentWindowProc(hWnd, uMsg, wParam, lParam, ret);
  
  

  return false;
}

void CDSPlayer::Reset(bool bForceWindowed)
{
  if (UsingDS(DIRECTSHOW_RENDERER_MPCVR))
    m_pAllocatorCallback->Reset(bForceWindowed);
}

// IDSRendererPaintCallback
//
// All four run on the application thread, every frame, and go straight into the surfaces the
// video renderer shares with madVR. Closing the graph destroys the renderer and those
// surfaces, so each has to join the same handshake CRenderDSManager::Render does - a null
// check on m_pPaintCallback is not enough, because the callback can be unregistered and the
// object destroyed between the test and the call. A Blu-ray rebuilding its graph does
// exactly that: it closes from the graph management thread while this one keeps rendering,
// and the crash lands in CMadvrSharedRender::BeginRender on freed textures.
void CDSPlayer::BeginRender()
{
  CRenderDSManager::CDrawingIntoDirectShow drawing(m_renderManager);
  if (!drawing)
    return;

  if (m_pPaintCallback && ReadyDS())
    m_pPaintCallback->BeginRender();
}

void CDSPlayer::RenderToTexture(DS_RENDER_LAYER layer)
{
  CRenderDSManager::CDrawingIntoDirectShow drawing(m_renderManager);
  if (!drawing)
    return;

  if (m_pPaintCallback && ReadyDS())
    m_pPaintCallback->RenderToTexture(layer);
}

void CDSPlayer::EndRender()
{
  // Not part of the handshake: this only clears Kodi's own back buffer
  m_renderManager.EndRender();

  CRenderDSManager::CDrawingIntoDirectShow drawing(m_renderManager);
  if (!drawing)
    return;

  if (m_pPaintCallback && ReadyDS())
    m_pPaintCallback->EndRender();
}

void CDSPlayer::IncRenderCount()
{
  CRenderDSManager::CDrawingIntoDirectShow drawing(m_renderManager);
  if (!drawing)
    return;

  if (m_pPaintCallback && ReadyDS())
    m_pPaintCallback->IncRenderCount();
}

// IMadvrSettingCallback
void CDSPlayer::RestoreSettings()
{
  if (m_pSettingCallback)
    m_pSettingCallback->RestoreSettings();
}

void CDSPlayer::LoadSettings(int iSectionId)
{
  if (m_pSettingCallback)
    m_pSettingCallback->LoadSettings(iSectionId);
}

void CDSPlayer::GetProfileActiveName(const std::string &path, std::string *profile)
{
  if (m_pSettingCallback)
    m_pSettingCallback->GetProfileActiveName(path, profile);
}
void CDSPlayer::OnSettingChanged(int iSectionId, CSettingsManager* settingsManager, const CSetting *setting)
{
  if (m_pSettingCallback)
    m_pSettingCallback->OnSettingChanged(iSectionId, settingsManager, setting);
}

void CDSPlayer::AddDependencies(const std::string &xml, CSettingsManager *settingsManager, CSetting *setting)
{
  if (m_pSettingCallback)
    m_pSettingCallback->AddDependencies(xml, settingsManager, setting);
}

void CDSPlayer::ListSettings(const std::string &path)
{
  if (m_pSettingCallback)
    m_pSettingCallback->ListSettings(path);
}

// IDSPlayer
bool CDSPlayer::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags)
{
  m_fps = fps;
  UpdateProcessInfo();
  bool configureres = m_renderManager.Configure(width, height, d_width, d_height, fps, flags);
  if (UsingDS(DIRECTSHOW_RENDERER_MADVR))
  {
    m_pAllocatorCallback->SetViewMode(CMediaSettings::GetInstance().GetDefaultVideoSettings().m_ViewMode);
  }
  return configureres;
}

bool CDSPlayer::UsingDS(DIRECTSHOW_RENDERER videoRenderer) const
{
  if (videoRenderer == DIRECTSHOW_RENDERER_UNDEF)
    videoRenderer = m_CurrentVideoRenderer;

  return (m_pAllocatorCallback != NULL && m_CurrentVideoRenderer == videoRenderer);
}

bool CDSPlayer::ReadyDS(DIRECTSHOW_RENDERER videoRenderer)
{
  if (videoRenderer == DIRECTSHOW_RENDERER_UNDEF)
    videoRenderer = m_CurrentVideoRenderer;

  return (m_pAllocatorCallback != NULL && m_renderOnDs && m_CurrentVideoRenderer == videoRenderer);
}

void CDSPlayer::ShowEditionDlg(bool playStart)
{
  UINT count = GetEditionsCount();

  if (count < 2)
    return;

  if (playStart && m_PlayerOptions.starttime > 0)
  {
    CDSPlayerDatabase db;
    if (db.Open())
    {
      CEdition edition;
      if (db.GetResumeEdition(m_currentFileItem.GetPath(), edition))
      {
        CLog::Log(LOGDEBUG, "{} select bookmark, edition with idx {} selected", __FUNCTION__, edition.editionNumber);
        SetEdition(edition.editionNumber);
        return;
      }
    }
  }

  g_dsGraph->Play(true);
  g_dsGraph->Pause();

  
  
  CGUIDialogSelect *dialog = (CGUIDialogSelect *)CServiceBroker::GetGUI()->GetWindowManager().GetWindow<CGUIDialogSelect>(WINDOW_DIALOG_SELECT);

  bool listAllTitles = false;
  UINT minLength = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_MINTITLELENGTH);

  while (true)
  {
    std::vector<UINT> editionOptions;

    dialog->SetHeading(IsMatroskaEditions() ? 55025 : 55026);

    CLog::Log(LOGDEBUG, "{} Edition count - {}", __FUNCTION__, count);
    int selected = GetEdition();
    for (UINT i = 0; i < count; i++)
    {
      std::string name;
      REFERENCE_TIME duration;

      GetEditionInfo(i, name, &duration);

      if (duration == _I64_MIN || listAllTitles || count == 1 || duration >= DS_TIME_BASE * 60 * minLength)
      {
        if (i == selected)
          selected = editionOptions.size();

        if (name.length() == 0)
          name = "Unnamed";
        dialog->Add(name);
        editionOptions.push_back(i);
      }
    }

    if (count > 1 && count != editionOptions.size())
    {
      dialog->Add(g_localizeStrings.Get(55027));
    }

    dialog->SetSelected(selected);
    dialog->Open();

    selected = dialog->GetSelectedItem();
    if (selected >= 0)
    {
      if (selected == editionOptions.size())
      {
        listAllTitles = true;
        continue;
      }
      UINT idx = editionOptions[selected];
      CLog::Log(LOGDEBUG, "{} edition with idx {} selected", __FUNCTION__, idx);
      SetEdition(idx);
      break;
    }
    break;
  }
}

CGraphManagementThread::CGraphManagementThread(CDSPlayer * pPlayer)
  : m_pPlayer(pPlayer), m_bSpeedChanged(false), CThread("CGraphManagementThread thread")
{
}

void CGraphManagementThread::OnStartup()
{
}

void CGraphManagementThread::Process()
{
  while (!this->m_bStop)
  {

    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;

    // Nothing the disc asks for may be acted on once the player is on its way out. A stop
    // ends the disc session, the disc answers by leaving its playlist, and answering *that*
    // by rebuilding the graph is what made stop restart the film rather than end it.
    const bool closing = CDSPlayer::PlayerState == DSPLAYER_CLOSING ||
                         CDSPlayer::PlayerState == DSPLAYER_CLOSED;

    if (!closing)
    {
      // The disc decides what plays; when it moves to another programme the graph has to be
      // built again around it, and this is the thread that can do it without holding up
      // either the interface or the disc.
      if (CDSPlayer::TakeDiscProgrammeChanged())
        m_pPlayer->RebuildGraphForDisc();

      // Smaller things a disc's menu can ask for, which the graph survives: a chapter, a
      // language, subtitles on or off
      m_pPlayer->ApplyWhatTheDiscAsked();
    }

    // This loop had no pause of its own and span flat out whenever the player was not in
    // its steady state, which starved the very threads a teardown waits for
    KODI::TIME::Sleep(10ms);

    if (m_bSpeedChanged)
    {
      m_pPlayer->GetClock().SetSpeed(m_currentRate * 1000);
      m_clockStart = m_pPlayer->GetClock().GetClock();
      m_bSpeedChanged = false;

      if (m_currentRate == 1)
        g_dsGraph->Play();

    }
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
    // Handle Rewind or Fast Forward
    if (m_currentRate != 1)
    {
      if (g_dsGraph->SetSpeed(m_currentRate))
      {
        if (g_dsGraph->GetTime() >= g_dsGraph->GetTotalTime()
          || g_dsGraph->GetTotalTime() <= 0)
        {
          m_currentRate = 1;
          m_pPlayer->GetPlayerCallback().OnPlayBackSpeedChanged(1);
          m_bSpeedChanged = true;
        }
      }
      else
      {
        double clock = m_pPlayer->GetClock().GetClock() - m_clockStart; // Time elapsed since the rate change
                                                                        // Only seek if elapsed time is greater than 250 ms
        if (abs(DS_TIME_TO_MSEC(clock)) >= 250)
        {
          //CLog::Log(LOGDEBUG, "Seeking time : %f", DS_TIME_TO_MSEC(clock));

          // New position
          uint64_t newPos = g_dsGraph->GetTime() + (uint64_t)clock;
          //CLog::Log(LOGDEBUG, "New position : %f", DS_TIME_TO_SEC(newPos));

          // Check boundaries
          if (newPos <= 0)
          {
            newPos = 0;
            m_currentRate = 1;
            m_pPlayer->GetPlayerCallback().OnPlayBackSpeedChanged(1);
            m_bSpeedChanged = true;
          }
          else if (newPos >= g_dsGraph->GetTotalTime())
          {
            CServiceBroker::GetAppMessenger()->PostMsg(TMSG_MEDIA_STOP);
            break;
          }

          g_dsGraph->Seek(newPos);

          m_clockStart = m_pPlayer->GetClock().GetClock();
        }
      }
    }
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED || CDSPlayer::PlayerState == DSPLAYER_CLOSING)
      break;

    // Handle rare graph event
    g_dsGraph->HandleGraphEvent();

    // Update displayed time
    g_dsGraph->UpdateTime();
    CThread::Sleep(250ms);
    
    if (CDSPlayer::PlayerState == DSPLAYER_CLOSED)
      break;
  }
}

#endif
