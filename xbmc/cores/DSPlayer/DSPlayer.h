#pragma once

/*
*      Copyright (C) 2005-2009 Team XBMC
*      http://www.xbmc.org
*
*	   Copyright (C) 2010-2013 Eduard Kytmanov
*	   http://www.avmedia.su
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

#include <chrono>
#include <vector>

#include "cores/IPlayer.h"
#include "threads/Thread.h"
#include "threads/SingleLock.h"
#include "DSGraph.h"
#include "DVDClock.h"
#include "url.h"
#include "StreamsManager.h"
#include "ChaptersManager.h"

#include "utils/TimeUtils.h"
#include "Threads/Event.h"
#include "dialogs/GUIDialogBoxBase.h"
#include "settings/Settings.h"

#include "pvr/PVRManager.h"
#include "application/Application.h"
#include "Videorenderers/RenderDSManager.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"

#if !defined(NPT_POINTER_TO_LONG)
#define NPT_POINTER_TO_LONG(_p) ((long)(_p))
#endif

#define START_PERFORMANCE_COUNTER { int64_t start = CurrentHostCounter();
#define END_PERFORMANCE_COUNTER(fn) int64_t end = CurrentHostCounter(); \
  CLog::Log(LOGINFO, "{} {}. Elapsed time: %.2fms", __FUNCTION__, fn, 1000.f * (end - start) / CurrentHostFrequency()); }

typedef DWORD ThreadIdentifier;

enum DSPLAYER_STATE
{
  DSPLAYER_LOADING,
  DSPLAYER_LOADED,
  DSPLAYER_PLAYING,
  DSPLAYER_PAUSED,
  DSPLAYER_STOPPED,
  DSPLAYER_CLOSING,
  DSPLAYER_CLOSED,
  DSPLAYER_ERROR
};

class CDSInputStreamPVRManager;
class CDSPlayer;
class CGraphManagementThread : public CThread
{
private:
  bool          m_bSpeedChanged;
  double        m_clockStart;
  double        m_currentRate;
  CDSPlayer*    m_pPlayer;
public:
  CGraphManagementThread(CDSPlayer * pPlayer);

  void SetSpeedChanged(bool value) { m_bSpeedChanged = value; }
  void SetCurrentRate(double rate) { m_currentRate = rate; }
  double GetCurrentRate() const { return m_currentRate; }
protected:
  void OnStartup();
  void Process();
};

class CDSPlayer : public IPlayer, public CThread, public IDispResource, public IRenderDSMsg, public IRenderLoop
{
  friend class CGraphManagementThread;

public:
  // IPlayer
  CDSPlayer(IPlayerCallback& callback);
  virtual ~CDSPlayer();
  virtual bool OpenFile(const CFileItem& file, const CPlayerOptions& options) override;
  virtual bool CloseFile(bool reopen = false) override;
  virtual bool IsPlaying() const override;
  virtual bool HasVideo() const override;
  virtual bool HasAudio() const override;
  virtual void Pause() override;
  virtual bool CanSeek() const  override { return g_dsGraph->CanSeek(); }
  virtual void Seek(bool bPlus, bool bLargeStep, bool bChapterOverride) override;
  virtual void SeekPercentage(float iPercent) override;
  virtual float GetPercentage() const override { return g_dsGraph->GetPercentage(); }
  virtual float GetCachePercentage() const override { return g_dsGraph->GetCachePercentage(); }
  virtual void SetVolume(float nVolume)  override { g_dsGraph->SetVolume(nVolume); }
  virtual void SetMute(bool bOnOff)  override { if (bOnOff) g_dsGraph->SetVolume(0.0f); }
  virtual void SetAVDelay(float fValue = 0.0f) override;
  virtual float GetAVDelay() override;

  virtual void SetSubTitleDelay(float fValue = 0.0f) override;
  virtual float GetSubTitleDelay() override;
  virtual int  GetSubtitleCount() const override;
  virtual int  GetSubtitle() override;
  virtual void GetSubtitleStreamInfo(int index, SubtitleStreamInfo& info) const override;
  virtual void SetSubtitle(int iStream) override;
  virtual bool GetSubtitleVisible() const override;
  virtual void SetSubtitleVisible(bool bVisible) override;
  virtual void AddSubtitle(const std::string& strSubPath) override;

  virtual int  GetAudioStreamCount() const override { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioStreamCount() : 0; }
  virtual int  GetAudioStream() override { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetAudioStream() : 0; }
  virtual void SetAudioStream(int iStream) override;

  //virtual int GetVideoStream() {} const override;
  virtual int GetVideoStreamCount() const override { return 1; }
  void GetVideoStreamInfo(int streamId, VideoStreamInfo& info) const override;
  virtual void GetVideoStreamInfo(int streamId, SPlayerVideoStreamInfo& info) const override;
  //virtual void SetVideoStream(int iStream);

  virtual int  GetChapterCount() const override { std::unique_lock<CCriticalSection> lock(m_StateSection); return CChaptersManager::Get()->GetChapterCount(); }
  virtual int  GetChapter() const override { std::unique_lock<CCriticalSection> lock(m_StateSection); return CChaptersManager::Get()->GetChapter(); }
  virtual void GetChapterName(std::string& strChapterName, int chapterIdx = -1) const override { std::unique_lock<CCriticalSection> lock(m_StateSection); CChaptersManager::Get()->GetChapterName(strChapterName, chapterIdx); }
  virtual int64_t GetChapterPos(int chapterIdx = -1) const override { return CChaptersManager::Get()->GetChapterPos(chapterIdx); }
  virtual int  SeekChapter(int iChapter) override { return CChaptersManager::Get()->SeekChapter(iChapter); }

  virtual void SeekTime(__int64 iTime = 0) override;
  virtual bool SeekTimeRelative(__int64 iTime) override;
  virtual __int64 GetTime() override;
  virtual __int64 GetTotalTime() override { std::unique_lock<CCriticalSection> lock(m_StateSection); return llrint(DS_TIME_TO_MSEC(g_dsGraph->GetTotalTime())); }
  virtual void SetSpeed(float iSpeed) override;
  virtual float GetSpeed() override;
  virtual bool SupportsTempo() override;
  virtual bool OnAction(const CAction& action) override;
  bool HasMenu() const override;
  bool IsInMenu() const override;
  MenuType GetSupportedMenuType() const override;
  void GetAudioStreamInfo(int index, AudioStreamInfo& info) const override;
#if TODO
  virtual bool SwitchChannel(const PVR::CPVRChannelPtr& channel) override;
#endif
  // RenderManager
  virtual void FrameMove() override;
  virtual void Render(bool clear, uint32_t alpha = 255, bool gui = true) override;
  virtual void FlushRenderer() override;
  virtual void SetRenderViewMode(int mode, float zoom, float par, float shift, bool stretch) override;
  virtual float GetRenderAspectRatio() const override;
  virtual void TriggerUpdateResolution() override;
  virtual bool IsRenderingVideo() const override;
  virtual bool IsRenderingGuiLayer() override;
  virtual bool IsRenderingVideoLayer() override;
  virtual bool Supports(EINTERLACEMETHOD method) const override;
  virtual bool Supports(ESCALINGMETHOD method) const override;
  virtual bool Supports(ERENDERFEATURE feature) const override;

  // IDSRendererAllocatorCallback
  CRect GetActiveVideoRect() override;
  bool IsEnteringExclusive() override;
  void EnableExclusive(bool bEnable) override;
  void SetPixelShader() const override;
  void SetResolution() const override;
  void SetPosition(CRect sourceRect, CRect videoRect, CRect viewRect) override;
  bool ParentWindowProc(HWND hWnd, UINT uMsg, WPARAM* wParam, LPARAM* lParam, LRESULT* ret) const override;
  void Reset(bool bForceWindowed) override;
  void DisplayChange(bool bExternalChange) override;

  // IDSRendererPaintCallback
  void BeginRender() override;
  void RenderToTexture(DS_RENDER_LAYER layer) override;
  void EndRender() override;
  void IncRenderCount() override;

  // IMadvrSettingCallback
  void LoadSettings(int iSectionId);
  void RestoreSettings();
  void GetProfileActiveName(const std::string& path, std::string* profile) override;
  void OnSettingChanged(int iSectionId, CSettingsManager* settingsManager, const CSetting* setting) override;
  void AddDependencies(const std::string& xml, CSettingsManager* settingsManager, CSetting* setting) override;
  void ListSettings(const std::string& path) override;

  // IDSPlayer
  bool Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags) override;
  bool UsingDS(DIRECTSHOW_RENDERER renderer = DIRECTSHOW_RENDERER_UNDEF) const override;
  bool ReadyDS(DIRECTSHOW_RENDERER renderer = DIRECTSHOW_RENDERER_UNDEF) override;
  void Register(IDSRendererAllocatorCallback* pAllocatorCallback) override { m_pAllocatorCallback = pAllocatorCallback; }
  void Register(IDSRendererPaintCallback* pPaintCallback) override { m_pPaintCallback = pPaintCallback; }
  void Register(IMadvrSettingCallback* pSettingCallback) override { m_pSettingCallback = pSettingCallback; }
  void Unregister(IDSRendererAllocatorCallback* pAllocatorCallback) override { m_pAllocatorCallback = nullptr; }
  void Unregister(IDSRendererPaintCallback* pPaintCallback) override { m_pPaintCallback = nullptr; }
  void Unregister(IMadvrSettingCallback* pSettingCallback) override { m_pSettingCallback = nullptr; }

  int  GetEditionsCount() override { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetEditionsCount() : 0; }
  int  GetEdition() override { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetEdition() : 0; }
  void GetEditionInfo(int iEdition, std::string& strEditionName, REFERENCE_TIME* prt) override { if (CStreamsManager::Get()) CStreamsManager::Get()->GetEditionInfo(iEdition, strEditionName, prt); };
  void SetEdition(int iEdition) override { if (CStreamsManager::Get()) CStreamsManager::Get()->SetEdition(iEdition); };
  bool IsMatroskaEditions() override { return (CStreamsManager::Get()) ? CStreamsManager::Get()->IsMatroskaEditions() : false; }
  void ShowEditionDlg(bool playStart) override;

  // IDispResource interface
  virtual void OnLostDisplay();
  virtual void OnResetDisplay();

  virtual bool IsCaching() const override { return false; }

  CVideoSettings GetVideoSettings() const override;
  void SetVideoSettings(CVideoSettings& settings) override;

  //CDSPlayer
  CDVDClock& GetClock() { return m_pClock; }
  IPlayerCallback& GetPlayerCallback() { return m_callback; }

  static DSPLAYER_STATE PlayerState;
  static CGUIDialogBoxBase* errorWindow;
  CFileItem m_currentFileItem;
  CPlayerOptions m_PlayerOptions;

  mutable CCriticalSection m_StateSection;
  CCriticalSection m_CleanSection;

  int GetPictureWidth() const { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetPictureWidth() : 0; }
  int GetPictureHeight() const { return (CStreamsManager::Get()) ? CStreamsManager::Get()->GetPictureHeight() : 0; }

  void GetGeneralInfo(std::string& strGeneralInfo);

  bool WaitForFileClose();
  bool OpenFileInternal(const CFileItem& file);
  void UpdateApplication();
  void UpdateChannelSwitchSettings();
  void LoadVideoSettings(const CFileItem& file);
  void SetPosition() const;

  void UpdateProcessInfo(int index = CURRENT_STREAM);
  void SetAudioCodeDelayInfo(int index = CURRENT_STREAM);

  //madVR Window
  static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
  static HWND m_hWnd;
  bool InitWindow(HWND& hWnd);
  void DeInitWindow();
  std::wstring m_className;
  HINSTANCE m_hInstance;
  bool m_isMadvr;

  CProcessInfo* m_processInfo;
  std::unique_ptr<CJobQueue> m_outboundEvents;

  static void PostMessage(CDSMsg* msg, bool wait = true)
  {
    // Closing is exactly when the graph has to be taken apart, so those two messages have to
    // get through a close rather than be dropped by it
    if (!m_threadID || PlayerState == DSPLAYER_CLOSED ||
        (PlayerState == DSPLAYER_CLOSING && msg->GetMessageType() != CDSMsg::PLAYER_STOP &&
         msg->GetMessageType() != CDSMsg::PLAYER_CLOSE_GRAPH))
    {
      CLog::Log(LOGWARNING, "CDSPlayer::PostMessage - message {} dropped, thread {} state {}",
                static_cast<int>(msg->GetMessageType()), m_threadID, static_cast<int>(PlayerState));
      msg->Release();
      return;
    }

    if (wait)
      msg->Acquire();

    //CLog::Log(LOGDEBUG, "{} Message posted : %d on thread 0x%X", __FUNCTION__, msg->GetMessageType(), m_threadID);
    if (!PostThreadMessage(m_threadID, WM_GRAPHMESSAGE, msg->GetMessageType(), (LPARAM)msg))
    {
      CLog::Log(LOGERROR, "CDSPlayer::PostMessage - could not post message {} to thread {}, error {}",
                static_cast<int>(msg->GetMessageType()), m_threadID, GetLastError());
    }

    if (wait)
    {
      msg->Wait();
      msg->Release();
    }
  }

  static HWND GetDShWnd() { return m_hWnd; }
  static bool IsDSPlayerThread() { return CThread::GetCurrentThread()->IsCurrentThread(); }

protected:
  void SetDSWndVisible(bool bVisible) override;
  void SetRenderOnDS(bool bRender) override;
  void VideoParamsChange() override;
  void GetDebugInfo(std::string &audio, std::string &video, std::string &general) override;

  void StopThread(bool bWait = true)
  {
    if (m_threadID)
    {
      PostThreadMessage(m_threadID, WM_QUIT, 0, 0);
      m_threadID = 0;
    }
    CThread::StopThread(bWait);
  }

  void HandleMessages();

  /*!
   * rief Wait, while still answering messages sent to this thread's windows
   *
   * This thread builds the graph and so owns the video renderer's window. Anything the
   * application thread does to the window it sits in - resizing it, going fullscreen -
   * sends a message to that child window and waits for this thread to answer. Sleeping
   * here without answering leaves the interface frozen for as long as the sleep, and
   * Windows kills an application whose window has not answered for long enough.
   */
  void SleepAnsweringMessages(std::chrono::milliseconds duration);

  /*!
   * rief Build the graph again for what a navigated disc has moved on to
   *
   * A splitter parses its stream once, when it connects, so it cannot follow a disc from
   * its menu to a title. The disc is left open across this so it carries on where it is.
   */
  void RebuildGraphForDisc();

public:
  /*!
   * rief Note that a navigated disc has moved to a different programme
   *
   * Left as a flag for the graph thread to pick up rather than sent as a message, because
   * it is raised on whichever thread the disc happened to be running on, which may be one
   * of the disc's own.
   */
  static void NoteDiscProgrammeChanged() { m_discProgrammeChanged = true; }
  static bool TakeDiscProgrammeChanged() { return m_discProgrammeChanged.exchange(false); }

  /*!
   * rief Whether the player is between programmes of a navigated disc
   *
   * Set while the file is closed only to be opened again on the programme the disc moved
   * to. The close must not be treated as the end of playback, and the open must reach the
   * player directly rather than wait for an end that is never announced.
   */
  static bool KeepingDiscOpen() { return m_keepDiscOpen; }

  /*!
   * \brief Note what a disc's menu has asked for, to be carried out shortly
   *
   * A popup menu can ask for things that do not need the graph rebuilding: another chapter,
   * another language, subtitles on or off. They arrive on the thread attending to the disc,
   * which must not be the one that goes and does them -- it is inside libbluray, and the
   * graph work it would be starting can come back round to the disc. Left here for the graph
   * management thread, the same way a change of programme is.
   * \{
   */
  static void NoteDiscWantsSeek(int64_t milliseconds) { m_discSeekWanted = milliseconds; }
  static void NoteDiscWantsAudioStream(int stream) { m_discAudioWanted = stream; }
  static void NoteDiscWantsSubtitleStream(int stream) { m_discSubtitleWanted = stream; }
  static void NoteDiscWantsSubtitles(bool visible) { m_discSubtitlesVisibleWanted = visible ? 1 : 0; }
  /*! \} */

  //! \brief Carry out whatever the disc's menu last asked for, on the graph thread
  void ApplyWhatTheDiscAsked();

  /*!
   * \brief Send the keyboard to whichever of the disc and Kodi is not getting it now
   *
   * Where input goes is normally decided by whether the disc has a menu drawn, which is the
   * honest question and the right one. It can still be wrong: a disc can leave a menu on
   * screen it will never take down, and then nothing the viewer presses opens the OSD ever
   * again. So the answer can always be overruled by hand, and the override is dropped the
   * moment the disc draws or clears a menu, since by then the automatic answer is fresh and
   * has more right to be believed than a decision made about the state before it.
   */
  void ToggleDiscInput();

  //! \brief Go back to deciding where input goes by what is on screen, see ToggleDiscInput
  static void ForgetDiscInputOverride();

private:
  static std::atomic<bool> m_discProgrammeChanged;
  //! What a disc's menu has asked for and nobody has done yet. -1 means nothing outstanding;
  //! a seek uses a negative time for the same, since a disc can legitimately ask for zero.
  static std::atomic<int64_t> m_discSeekWanted;
  static std::atomic<int> m_discAudioWanted;
  static std::atomic<int> m_discSubtitleWanted;
  static std::atomic<int> m_discSubtitlesVisibleWanted;
  //! Where the viewer has insisted their key presses go: -1 to decide it from what is on
  //! screen, 0 for Kodi, 1 for the disc. See ToggleDiscInput.
  static std::atomic<int> m_discInputForced;
  //! Set while the file is being closed only to reopen it on what the disc moved to, so the
  //! disc is not finished with in between
  static bool m_keepDiscOpen;


  bool ShowPVRChannelInfo();

  CGraphManagementThread m_pGraphThread;
  CDVDClock m_pClock;
  CEvent m_hReadyEvent;
  static ThreadIdentifier m_threadID;
  bool m_bEof;

  bool SelectChannel(bool bNext);
  bool SwitchChannel(unsigned int iChannelNumber);
  void LoadMadvrSettings(int id);
  void SetCurrentVideoRenderer(const std::string &videoRenderer);

  // CThread
  virtual void OnStartup() override;
  virtual void OnExit() override;
  virtual void Process() override;

  bool m_HasVideo;
  bool m_HasAudio;

  std::atomic_bool m_canTempo;

  CRenderDSManager m_renderManager;

  float m_fps;
  CRect m_sourceRect;
  CRect m_videoRect;
  CRect m_viewRect;

  void SetVisibleScreenArea(CRect activeVideoRect);
  int VideoDimsToResolution(int iWidth, int iHeight);
  CRect m_lastActiveVideoRect;

  IDSRendererAllocatorCallback* m_pAllocatorCallback;
  IMadvrSettingCallback* m_pSettingCallback;
  IDSRendererPaintCallback* m_pPaintCallback;
  bool m_renderOnDs;
  bool m_bPerformStop;
  int m_renderUnderCount;
  int m_renderOverCount;
  DS_RENDER_LAYER m_currentVideoLayer;
  DIRECTSHOW_RENDERER m_CurrentVideoRenderer;

  //added from videoplayer let to see if its what we need
  struct SContent
  {
    mutable CCriticalSection m_section;
    //CSelectionStreams m_selectionStreams;

    std::vector<ProgramInfo> m_programs;
    int m_videoIndex{ -1 };
    int m_audioIndex{ -1 };
    int m_subtitleIndex{ -1 };
  } m_content;
};

