/*
*  Copyright (C) 2010-2013 Eduard Kytmanov
*  http://www.avmedia.su
*
*  Copyright (C) 2015 Romank
*  https://github.com/Romank1
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
*  along with GNU Make; see the file COPYING.  If not, write to
*  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*  http://www.gnu.org/copyleft/gpl.html
*
*/

#if HAS_DS_PLAYER

#include "DSInputStreamPVRManager.h"
#include "URL.h"

#include "pvr/addons/PVRClients.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "settings/AdvancedSettings.h"
#include "utils/StringUtils.h"
#include "pvr/recordings/PVRRecordingsPath.h"
#include "ServiceBroker.h"

#if TODO
CDSInputStreamPVRManager* g_pPVRStream = NULL;

CDSInputStreamPVRManager::CDSInputStreamPVRManager(CDSPlayer *pPlayer)
  : m_pPlayer(pPlayer)
  , m_pPVRBackend(NULL)
{
}

CDSInputStreamPVRManager::~CDSInputStreamPVRManager(void)
{
  Close();
}

void CDSInputStreamPVRManager::Close()
{
  
#if TODO
  g_PVRManager.CloseStream();
#endif
  SAFE_DELETE(m_pPVRBackend);
}

bool CDSInputStreamPVRManager::CloseAndOpenFile(const CURL& url)
{
  std::string strURL = url.Get();

  // In case opened channel need to be closed before opening new channel
  bool bReturnVal = false;
  if ( CDSPlayer::PlayerState == DSPLAYER_PLAYING 
    || CDSPlayer::PlayerState == DSPLAYER_PAUSED 
    || CDSPlayer::PlayerState == DSPLAYER_STOPPED)
  {
    // New channel cannot be opened, try to close the file
    m_pPlayer->CloseFile(false);
    bReturnVal = m_pPlayer->WaitForFileClose();

    if (!bReturnVal)
      CLog::Log(LOGERROR, "{} Closing file failed", __FUNCTION__);

    if (bReturnVal)
    {
      // Try to open the file
      bReturnVal = false;
      for (int iRetry = 0; iRetry < 10 && !bReturnVal; iRetry++)
      {
        CPVRManager& mgr = CServiceBroker::GetPVRManager();
#if TODO
        std::shared_ptr<CPVRChannel> tag = mgr.ChannelGroups()->GetByPath(strURL);
        if (tag && tag->HasPVRChannelInfoTag())
        {
          if (g_PVRManager.OpenLiveStream(*tag))
            bReturnVal = true;
          else
            Sleep(500);
        }
#endif
      }
      if (!bReturnVal)
        CLog::Log(LOGERROR, "{} Opening file failed", __FUNCTION__);
    }
  }

  if (bReturnVal)
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "DSPlayer", "File opened successfully", TOAST_MESSAGE_TIME, false);

  return bReturnVal;
}


std::string CDSInputStreamPVRManager::TranslatePVRFilename(const std::string& pathFile)
{
#if TODO
  if (!g_PVRManager.IsStarted())
    return "";
#endif

  std::string FileName = pathFile;
#if TODO
  if (FileName.substr(0, 14) == "pvr://channels")
  {
    std::shared_ptr<CFileItemPtr> channel = g_PVRChannelGroups->GetByPath(FileName);
    if (channel && channel->HasPVRChannelInfoTag())
    {
      std::string stream = channel->GetPVRChannelInfoTag()->StreamURL();
      if (!stream.empty())
      {
        if (stream.compare(6, 7, "stream/") == 0)
        {
          // pvr://stream
          // This function was added to retrieve the stream URL for this item
          // Is is used for the MediaPortal (ffmpeg) PVR addon
          // see PVRManager.cpp
          return g_PVRClients->GetStreamURL(channel->GetPVRChannelInfoTag());
        }
        else
        {
          return stream;
        }
      }
    }
  }
#endif
  return FileName;

}

bool CDSInputStreamPVRManager::Open(const CFileItem& file)
{
  Close();

  bool bReturnVal = false;
  std::string strTranslatedPVRFile;

  CURL url(file.GetPath());

  std::string strURL = url.Get();

  if (StringUtils::StartsWith(strURL, "pvr://channels/tv/") ||
    StringUtils::StartsWith(strURL, "pvr://channels/radio/"))
  {
    CFileItemPtr tag = g_PVRChannelGroups->GetByPath(strURL);
    if (tag && tag->HasPVRChannelInfoTag())
    {
      if (!g_PVRManager.OpenLiveStream(*tag))
        return false;

      bReturnVal = true;
      m_isRecording = false;
      CLog::Log(LOGDEBUG, "CDSInputStreamPVRManager - {} - playback has started on filename {}", __FUNCTION__, strURL.c_str());
    }
    else
    {
      CLog::Log(LOGERROR, "CDSInputStreamPVRManager - {} - channel not found with filename {}", __FUNCTION__, strURL.c_str());
      return false;
    }
  }

  if (!bReturnVal)
    bReturnVal = CloseAndOpenFile(url);

  if (bReturnVal)
  {
    m_pPVRBackend = GetPVRBackend();

    if (file.IsLiveTV())
    {
      bReturnVal = true;
      strTranslatedPVRFile = TranslatePVRFilename(file.GetPath());
      if (strTranslatedPVRFile == file.GetPath())
      {
        if (file.HasPVRChannelInfoTag())
          strTranslatedPVRFile = g_PVRClients->GetStreamURL(file.GetPVRChannelInfoTag());
      }

      // Check if LiveTV file path is valid for DSPlayer.
      if (URIUtils::IsLiveTV(strTranslatedPVRFile))
        bReturnVal = false;

      // Convert Stream URL To TimeShift file path 
      if (bReturnVal && CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_bDSPlayerUseUNCPathsForLiveTV)
      {
        if (m_pPVRBackend && m_pPVRBackend->SupportsStreamConversion(strTranslatedPVRFile))
        {
          std::string strTimeShiftFile;
          bReturnVal = m_pPVRBackend->ConvertStreamURLToTimeShiftFilePath(strTranslatedPVRFile, strTimeShiftFile);
          if (bReturnVal)
            strTranslatedPVRFile = strTimeShiftFile;
        }
        else
        {
          CLog::Log(LOGERROR, "{} Stream conversion is not supported for this PVR Backend url: {}", __FUNCTION__, strTranslatedPVRFile.c_str());
        }
      }
    }
    else if (m_pPVRBackend && file.IsPVRRecording())
    {
      if (file.HasPVRRecordingInfoTag())
      {
        CPVRRecordingPtr recordingPtr = file.GetPVRRecordingInfoTag();
        std::string strRecordingUrl;
        bReturnVal = m_pPVRBackend->GetRecordingStreamURL(recordingPtr->m_strRecordingId, strRecordingUrl, CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_bDSPlayerUseUNCPathsForLiveTV);
        if (bReturnVal)
          strTranslatedPVRFile = strRecordingUrl;
      }
    }
  }

  if (bReturnVal)
  {
    CFileItem fileItem = file;
    fileItem.SetPath(strTranslatedPVRFile);
    bReturnVal = m_pPlayer->OpenFileInternal(fileItem);
  }
  else
  {
    std::string strMessage = StringUtils::Format("Opening {} failed", file.IsLiveTV() ? "channel" : "recording");
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "DSPlayer", strMessage, TOAST_DISPLAY_TIME, false);
    CLog::Log(LOGERROR, "{} {}", __FUNCTION__, strMessage.c_str());
  }

  return bReturnVal;
}

bool  CDSInputStreamPVRManager::PrepareForChannelSwitch(const CPVRChannelPtr &channel)
{
  bool bReturnVal = true;
  // Workaround for MediaPortal addon, running in ffmpeg mode.
  if (m_pPVRBackend && m_pPVRBackend->GetBackendName().find("MediaPortal TV-server") != std::string::npos)
  {
    // Opened new channel manually.
    // This will prevent from SwitchChannel() method to fail.
    bReturnVal = !((g_PVRClients->GetStreamURL(channel)).empty());

    // Clear StreamURL field of CurrentChannel 
    // that's used in CPVRClients::SwitchChannel()
    g_PVRManager.GetCurrentChannel()->SetStreamURL("");

    // Clear StreamURL field of new channel that's
    // used in CPVRManager::ChannelSwitch()
    channel->SetStreamURL("");
  }

  return bReturnVal;
}

bool CDSInputStreamPVRManager::PerformChannelSwitch()
{
  bool bResult = false;
  CFileItem fileItem;
  bResult = GetNewChannel(fileItem);
  if (bResult)
  {
    if (m_pPlayer->m_currentFileItem.GetPath() != fileItem.GetPath())
    {
      // File changed - fast channel switching is not possible
      CLog::Log(LOGINFO, "{} - File changed - fast channel switching is not possible, opening new channel...", __FUNCTION__);
      bResult = m_pPlayer->OpenFileInternal(fileItem);
    }
    else
    {
      // File not changed - channel switch complete successfully.
      m_pPlayer->UpdateApplication();
      m_pPlayer->UpdateChannelSwitchSettings();
      CLog::Log(LOGINFO, "{} - Channel switch complete successfully", __FUNCTION__);
    }
  }

  return bResult;
}

bool CDSInputStreamPVRManager::GetNewChannel(CFileItem& fileItem)
{
  bool bResult = false;
  CPVRChannelPtr channelPtr(g_PVRManager.GetCurrentChannel());
  if (channelPtr)
  {
    std::string strNewFile = g_PVRClients->GetStreamURL(channelPtr);
    if (!strNewFile.empty())
    {
      bResult = true;
      // Convert Stream URL To TimeShift file
      if (CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_bDSPlayerUseUNCPathsForLiveTV && m_pPVRBackend && m_pPVRBackend->SupportsStreamConversion(strNewFile))
      {
        std::string timeShiftFile = "";
        if (m_pPVRBackend->ConvertStreamURLToTimeShiftFilePath(strNewFile, timeShiftFile))
          strNewFile = timeShiftFile;
      }

      CFileItem newFileItem(channelPtr);
      fileItem = newFileItem;
      fileItem.SetPath(strNewFile);
      CLog::Log(LOGINFO, "{} - New channel file path: {}", __FUNCTION__, strNewFile.c_str());
    }
  }

  if (!bResult)
    CLog::Log(LOGERROR, "{} - Failed to get file path of the new channel", __FUNCTION__);

  return bResult;
}

bool CDSInputStreamPVRManager::SelectChannel(const CPVRChannelPtr &channel)
{
  bool bResult = false;

  {
    assert(channel.get());

    if (!SupportsChannelSwitch())
    {
      CFileItem item(channel);
      bResult = Open(item);
    }
    else if (PrepareForChannelSwitch(channel))
    {
      bResult = g_PVRManager.ChannelSwitchById(channel->ChannelID());
      if (bResult)
        bResult = PerformChannelSwitch();
    }
  }
  return bResult;
}

bool CDSInputStreamPVRManager::NextChannel(bool preview /* = false */)
{
  bool bResult = false;
  PVR_CLIENT client;
  unsigned int newchannel;

  CPVRChannelPtr channel(g_PVRManager.GetCurrentChannel());
  CFileItemPtr item = g_PVRChannelGroups->Get(channel->IsRadio())->GetSelectedGroup()->GetByChannelUp(channel);
  if (!item || !item->HasPVRChannelInfoTag())
  {
    CLog::Log(LOGERROR, "{} - Cannot find the channel", __FUNCTION__);
    return false;
  }

  if (!preview && !SupportsChannelSwitch())
  {
    if (item.get())
      bResult = Open(*item.get());
  }
  else if (PrepareForChannelSwitch(item->GetPVRChannelInfoTag()))
  {
    bResult = g_PVRManager.ChannelUp(&newchannel,preview);
    if (bResult)
      bResult = PerformChannelSwitch();
  }

  return bResult;
}

bool CDSInputStreamPVRManager::PrevChannel(bool preview/* = false*/)
{
  bool bResult = false;
  PVR_CLIENT client;
  unsigned int newchannel;

  CPVRChannelPtr channel(g_PVRManager.GetCurrentChannel());
  CFileItemPtr item = g_PVRChannelGroups->Get(channel->IsRadio())->GetSelectedGroup()->GetByChannelDown(channel);
  if (!item || !item->HasPVRChannelInfoTag())
  {
    CLog::Log(LOGERROR, "{} - Cannot find the channel", __FUNCTION__);
    return false;
  }

  if (!preview && !SupportsChannelSwitch())
  {
    if (item.get())
      bResult = Open(*item.get());
  }
  else if (PrepareForChannelSwitch(item->GetPVRChannelInfoTag()))
  {
    bResult = g_PVRManager.ChannelDown(&newchannel, preview);
    if (bResult)
      bResult = PerformChannelSwitch();
  }

  return bResult;
}

bool CDSInputStreamPVRManager::SelectChannelByNumber(unsigned int iChannelNumber)
{
  bool bResult = false;
  PVR_CLIENT client;

  CPVRChannelPtr channel(g_PVRManager.GetCurrentChannel());
  CFileItemPtr item = g_PVRChannelGroups->Get(channel->IsRadio())->GetSelectedGroup()->GetByChannelNumber(iChannelNumber);
  if (!item || !item->HasPVRChannelInfoTag())
  {
    CLog::Log(LOGERROR, "{} - Cannot find the channel %d", __FUNCTION__, iChannelNumber);
    return false;
  }

  if (!SupportsChannelSwitch())
  {
    if (item.get())
      bResult = Open(*item.get());
  }
  else if (PrepareForChannelSwitch(item->GetPVRChannelInfoTag()))
  {
    bResult = g_PVRManager.ChannelSwitchById(item->GetPVRChannelInfoTag()->ChannelID());
    if (bResult)
      bResult = PerformChannelSwitch();
  }

  return bResult;
}

bool CDSInputStreamPVRManager::SupportsChannelSwitch()const
{
  if (!m_pPVRBackend || !CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_bDSPlayerFastChannelSwitching)
    return false;

  PVR_CLIENT pvrClient;
  if (!g_PVRClients->GetPlayingClient(pvrClient))
    return false;

  // Check if active PVR Backend addon has changed or PVR Backend addon does not support channel switching
  if (pvrClient->GetBackendName() != m_pPVRBackend->GetBackendName() || !m_pPVRBackend->SupportsFastChannelSwitch())
    return false;

  return pvrClient->HandlesInputStream();
}

bool CDSInputStreamPVRManager::UpdateItem(CFileItem& item)
{
  return g_PVRManager.UpdateItem(item);
}

CDSPVRBackend* CDSInputStreamPVRManager::GetPVRBackend()
{
  PVR_CLIENT pvrClient;
  if (!g_PVRClients->GetPlayingClient(pvrClient))
  {
    CLog::Log(LOGERROR, "{} - Failed to get PVR Client", __FUNCTION__);
    return NULL;
  }

  CDSPVRBackend* pPVRBackend = NULL;
  if (pvrClient->GetBackendName().find("MediaPortal TV-server") != std::string::npos)
  {
    pPVRBackend = new CDSMediaPortal(pvrClient->GetConnectionString(), pvrClient->GetBackendName());
  }
  else if (pvrClient->GetBackendName().find("ARGUS TV") != std::string::npos)
  {
    pPVRBackend = new CDSArgusTV(pvrClient->GetConnectionString(), pvrClient->GetBackendName());
  }

  return pPVRBackend;
}

uint64_t CDSInputStreamPVRManager::GetTotalTime()
{
  if (!m_isRecording)
    return g_PVRManager.GetTotalTime();
  return 0;
}

uint64_t CDSInputStreamPVRManager::GetTime()
{
  if (!m_isRecording)
    return g_PVRManager.GetStartTime();
  return 0;
}

#endif
#endif