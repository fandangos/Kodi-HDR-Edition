/*
 *      Copyright (C) 2005-2010 Team XBMC
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

#include "StreamsManager.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/StreamDetails.h"
#include "filesystem/SpecialProtocol.h"
#include "DVDSubtitles/DVDFactorySubtitle.h"
#include "settings/Settings.h"
#include "filtercorefactory/filtercorefactory.h"
#include "DSPlayer.h"
#include "FGFilter.h"
#include "SComCli.h"
#include "windowing/windows/WinSystemWin32DX.h"
#include "LangInfo.h"
#include "settings/MediaSettings.h"
#include "Filters/RendererSettings.h"
#include "Utils/URIUtils.h"
#include "utils/StringUtils.h"
#include "utils/DSFileUtils.h"
#include "guilib/LocalizeStrings.h"
#include "LangInfo.h"
#include "DSPlayerDatabase.h"
#include "guilib/StereoscopicsManager.h"
#include "DSPropertyPage.h"
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "Util.h"
#include "application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "guilib/GUIComponent.h"
#include "guilib/StereoscopicsManager.h"

#include <algorithm>

#pragma comment(lib , "libsubs.lib")

CDSStreamDetail::CDSStreamDetail()
  : IAMStreamSelect_Index(0), flags(0), pObj(NULL), pUnk(NULL), lcid(0),
  group(0), displayname(""), connected(false)
{
}

CDSStreamDetailAudio::CDSStreamDetailAudio()
  : m_strCodecName(""), m_iBitRate(0), m_iSampleRate(0)
{
}

CDSStreamDetailVideo::CDSStreamDetailVideo()
  : m_strCodecName("")
{
}

CDSStreamDetailSubtitle::CDSStreamDetailSubtitle(SubtitleType type)
  : m_subType(type), encoding(""), isolang(""), trackname(""), offset(0), subtype(GUID_NULL)
{
}

CDSStreamDetailSubfilter::CDSStreamDetailSubfilter(SubtitleType type)
  : m_subType(type), encoding(""), isolang(""), trackname(""), offset(0), subtype(GUID_NULL)
{
}

CDSStreamDetailSubtitleExternal::CDSStreamDetailSubtitleExternal()
  : CDSStreamDetailSubtitle(EXTERNAL), path(""), substream(NULL)
{
}

CStreamsManager *CStreamsManager::m_pSingleton = NULL;

CStreamsManager *CStreamsManager::Get()
{
  return m_pSingleton;
}

CStreamsManager::CStreamsManager(void)
  : m_pIAMStreamSelect(NULL)
  , m_init(false)
  , m_readyEvent(true)
  , m_dvdStreamLoaded(false)
  , m_mkveditions(false)
  , m_pIAMStreamSelectSub(NULL)
  , m_pIDirectVobSub(NULL)
{
  m_readyEvent.Set();
  m_lastDelay = 0;
}

CStreamsManager::~CStreamsManager(void)
{

  while (!m_audioStreams.empty())
  {
    delete m_audioStreams.back();
    m_audioStreams.pop_back();
  }
  while (!m_editionStreams.empty())
  {
    delete m_editionStreams.back();
    m_editionStreams.pop_back();
  }
  while (!m_subfilterStreams.empty())
  {
    delete m_subfilterStreams.back();
    m_subfilterStreams.pop_back();
  }

  m_pSplitter = NULL;
  m_pGraphBuilder = NULL;
  m_pSubs = NULL;

  CLog::Log(LOGDEBUG, "{} Ressources released", __FUNCTION__);

}

std::vector<CDSStreamDetailAudio *>& CStreamsManager::GetAudios()
{
  return m_audioStreams;
}

int CStreamsManager::GetAudioStreamCount()
{
  return m_audioStreams.size();
}

int CStreamsManager::GetAudioStream()
{
  if (m_audioStreams.size() == 0)
    return -1;

  int i = 0;
  for (std::vector<CDSStreamDetailAudio *>::const_iterator it = m_audioStreams.begin();
    it != m_audioStreams.end(); ++it, i++)
  {
    if ((*it)->flags & AMSTREAMSELECTINFO_ENABLED)
      return i;
  }

  return -1;
}

void CStreamsManager::GetAudioStreamName(int iStream, std::string &strStreamName)
{
  if (m_audioStreams.size() == 0)
    return;

  int i = 0;
  for (std::vector<CDSStreamDetailAudio *>::const_iterator it = m_audioStreams.begin();
    it != m_audioStreams.end(); ++it, i++)
  {
    if (i == iStream)
    {
      strStreamName = (*it)->displayname;
    }
  }
}

void CStreamsManager::GetAudioStreamLanguage(int iStream, std::string &strStreamLang)
{
  if (m_audioStreams.size() == 0)
    return;

  int i = 0;
  for (std::vector<CDSStreamDetailAudio *>::const_iterator it = m_audioStreams.begin();
    it != m_audioStreams.end(); ++it, i++)
  {
    if (i == iStream && (*it)->lcid)
    {
      wchar_t buffer[64];
      int len = 0;
      if (len = GetLocaleInfo((*it)->lcid, LOCALE_SISO639LANGNAME, buffer, 64))
      {
        strStreamLang = WToA(buffer);
        strStreamLang.resize(len - 1); //get rid of last \0
      }
    }
  }
}

void CStreamsManager::GetVideoStreamName(std::string &strStreamName)
{
    strStreamName = m_videoStream.displayname;
}

void CStreamsManager::SetAudioStream(int iStream)
{
  if (!m_init || m_audioStreams.size() == 0)
    return;

  if (CGraphFilters::Get()->IsDVD())
    return; // currently not implemented

  CSingleExit lock(m_lock);

  long disableIndex = GetAudioStream(), enableIndex = iStream;

  if (disableIndex == enableIndex
    || !m_audioStreams[disableIndex]->connected
    || m_audioStreams[enableIndex]->connected)
    return;

  m_readyEvent.Reset();
  XbmcThreads::CAutoSetEvent event(&m_readyEvent);

  if (m_pIAMStreamSelect)
  {
    m_audioStreams[disableIndex]->connected = false;
    m_audioStreams[disableIndex]->flags = 0;

    if (SUCCEEDED(m_pIAMStreamSelect->Enable(m_audioStreams[enableIndex]->IAMStreamSelect_Index, AMSTREAMSELECTENABLE_ENABLE)))
    {
      m_audioStreams[enableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
      m_audioStreams[enableIndex]->connected = true;
      CLog::Log(LOGDEBUG, "{} Successfully selected audio stream index: %i", __FUNCTION__, m_audioStreams[enableIndex]->IAMStreamSelect_Index);
    }
  }
  else {

    Com::SComPtr<IPin> connectedToPin = NULL;
    HRESULT hr = S_OK;
#if 0
    std::string filter = "";
    SVideoStreamIndexes indexes(0, iStream, GetSubtitle());

    if (SUCCEEDED(CFilterCoreFactory::GetAudioFilter(CDSPlayer::currentFileItem, filter,
      CFGLoader::IsUsingDXVADecoder(), &indexes)))
    {
      CFGFilterFile *f;
      if (!(f = CFilterCoreFactory::GetFilterFromName(filter, false)))
      {
        CLog::Log(LOGERROR, "{} The filter corresponding to the new rule doesn't exist. Using current audio decoder to decode audio.", __FUNCTION__);
        goto standardway;
      }
      else
      {
        if (CFGLoader::Filters.Audio.guid == f->GetCLSID())
          goto standardway; // Same filter

        // Where this audio decoder is connected ?
        Com::SComPtr<IPin> audioDecoderOutputPin = GetFirstPin(CFGLoader::Filters.Audio.pBF, PINDIR_OUTPUT); // first output pin of audio decoder
        Com::SComPtr<IPin> audioDecoderConnectedToPin = NULL;
        hr = audioDecoderOutputPin->ConnectedTo(&audioDecoderConnectedToPin);

        if (!audioDecoderConnectedToPin)
          goto standardway;

        audioDecoderOutputPin = NULL;

        m_pGraph->Stop(); // Current position is kept by the graph

        // Remove the old decoder from the graph
        hr = m_pGraphBuilder->RemoveFilter(CFGLoader::Filters.Audio.pBF);
        if (SUCCEEDED(hr))
          CLog::Log(LOGDEBUG, "{} Removed old audio decoder from the graph", __FUNCTION__);

        // Delete filter
        m_audioStreams[disableIndex]->pUnk = NULL;
        m_audioStreams[enableIndex]->pUnk = NULL;
        CFGLoader::Filters.Audio.pBF.FullRelease();

        // Create the new audio decoder
        if (FAILED(f->Create(&CFGLoader::Filters.Audio.pBF)))
          goto standardway;

        // Change references
        CFGLoader::Filters.Audio.guid = f->GetCLSID();
        g_charsetConverter.wToUTF8(f->GetName(), CFGLoader::Filters.Audio.osdname);

        // Add the new one
        if (FAILED(hr = m_pGraphBuilder->AddFilter(CFGLoader::Filters.Audio.pBF, f->GetName())))
        {
          CLog::Log(LOGERROR, "{} Failed to add the new audio decoder. No more sound!", __FUNCTION__);
          goto done;
        }

        // Make connections
        m_audioStreams[enableIndex]->pUnk = GetFirstPin(CFGLoader::Filters.Audio.pBF);
        hr = m_pGraphBuilder->ConnectDirect(m_audioStreams[enableIndex]->pObj, m_audioStreams[enableIndex]->pUnk, NULL);

        Com::SComPtr<IPin> pin = NULL;
        pin = GetFirstPin(CFGLoader::Filters.Audio.pBF, PINDIR_OUTPUT);
        hr = m_pGraphBuilder->ConnectDirect(pin, audioDecoderConnectedToPin, NULL);

        m_audioStreams[disableIndex]->flags = 0;
        m_audioStreams[disableIndex]->connected = false;

        m_audioStreams[enableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
        m_audioStreams[enableIndex]->connected = true;

        CLog::Log(LOGINFO, "{} New audio decoder \"{}\" inserted because of rules configuration.", __FUNCTION__, CFGLoader::Filters.Audio.osdname.c_str());

        // Connections done, run the graph!
        goto done;
      }

    }
#endif

    /* Disable filter */
    //standardway:

    // Stop the graph
    g_dsGraph->Stop();

    IPin *newAudioStreamPin = m_audioStreams[enableIndex]->pObj; // Splitter pin
    IPin *oldAudioStreamPin = m_audioStreams[disableIndex]->pObj; // Splitter pin

    connectedToPin = m_audioStreams[disableIndex]->pUnk; // Audio dec pin
    if (!connectedToPin) // This case shouldn't happened
      hr = oldAudioStreamPin->ConnectedTo(&connectedToPin);

    if (FAILED(hr))
      goto done;

    m_audioStreams[enableIndex]->pUnk = connectedToPin;

    hr = m_pGraphBuilder->Disconnect(connectedToPin);
    if (FAILED(hr))
      goto done;
    hr = m_pGraphBuilder->Disconnect(oldAudioStreamPin);
    if (FAILED(hr))
    {
      /* Reconnect pin */
      m_pGraphBuilder->ConnectDirect(oldAudioStreamPin, connectedToPin, NULL);
      goto done;
    }

    m_audioStreams[disableIndex]->flags = 0;
    m_audioStreams[disableIndex]->connected = false;

    /* Enable filter */
    hr = m_pGraphBuilder->ConnectDirect(newAudioStreamPin, connectedToPin, NULL);
    if (FAILED(hr))
    {
      /* Reconnect previous working pins */
      m_pGraphBuilder->ConnectDirect(oldAudioStreamPin, connectedToPin, NULL);
      m_audioStreams[disableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
      m_audioStreams[disableIndex]->connected = true;
      goto done;
    }

    m_audioStreams[enableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
    m_audioStreams[enableIndex]->connected = true;

  done:
    g_dsGraph->Play();

    if (SUCCEEDED(hr))
      CLog::Log(LOGINFO, "{} Successfully changed audio stream", __FUNCTION__);
    else
      CLog::Log(LOGERROR, "{} Can't change audio stream", __FUNCTION__);
  }
}

int CStreamsManager::GetEditionsCount()
{
  return m_editionStreams.size();
}

int CStreamsManager::GetEdition()
{
  if (m_editionStreams.size() == 0)
    return -1;

  int i = 0;
  for (std::vector<CDSStreamDetailEdition *>::const_iterator it = m_editionStreams.begin();
    it != m_editionStreams.end(); ++it, i++)
  {
    if ((*it)->flags & AMSTREAMSELECTINFO_ENABLED)
      return i;
  }

  return -1;
}

void CStreamsManager::GetEditionInfo(int iEdition, std::string &strEditionName, REFERENCE_TIME *prt)
{
  if (m_editionStreams.size() == 0)
    return;

  int i = 0;
  for (std::vector<CDSStreamDetailEdition *>::const_iterator it = m_editionStreams.begin();
    it != m_editionStreams.end(); ++it, i++)
  {
    if (i == iEdition)
    {

      strEditionName = (*it)->displayname.c_str();
      if (prt)*prt = (*it)->m_rtDuration;
    }
  }
}

void CStreamsManager::SetEdition(int iEdition)
{
  if (!m_init)
    return;

  if (CGraphFilters::Get()->IsDVD())
    return; // currently not implemented

  CSingleExit lock(m_lock);

  long disableIndex = GetEdition(), enableIndex = iEdition;

  if (disableIndex == enableIndex)
    return;

  m_readyEvent.Reset();
  XbmcThreads::CAutoSetEvent event(&m_readyEvent);

  if (m_pIAMStreamSelect)
  {
    m_editionStreams[disableIndex]->flags = 0;
    m_editionStreams[disableIndex]->connected = false;

    if (SUCCEEDED(m_pIAMStreamSelect->Enable(m_editionStreams[enableIndex]->IAMStreamSelect_Index, AMSTREAMSELECTENABLE_ENABLE)))
    {
      // Reload Chapters
      if (!CChaptersManager::Get()->LoadChapters())
        CLog::Log(LOGINFO, "{} No chapters found!", __FUNCTION__);

      // Update displayed time
      g_dsGraph->UpdateTotalTime();

      m_editionStreams[enableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
      m_editionStreams[enableIndex]->connected = true;
      CLog::Log(LOGDEBUG, "{} Successfully selected edition - index: %i", __FUNCTION__, m_editionStreams[enableIndex]->IAMStreamSelect_Index);
    }
  }
}

void CStreamsManager::LoadIAMStreamSelectStreamsInternal()
{
  DWORD nStreams = 0, flags = 0, group = 0;
  WCHAR* wname = NULL;
  CStreamDetail* infos = NULL;
  LCID lcid;
  IUnknown *pObj = NULL, *pUnk = NULL;
  m_mkveditions = false;
  m_pIAMStreamSelect->Count(&nStreams);

  AM_MEDIA_TYPE * mediaType = NULL;
  for (unsigned int i = 0; i < nStreams; i++)
  {
    m_pIAMStreamSelect->Info(i, &mediaType, &flags, &lcid, &group, &wname, &pObj, &pUnk);

    switch (group)
    {
    case CStreamDetail::VIDEO:
      infos = &m_videoStream;
      break;
    case CStreamDetail::AUDIO:
      infos = new CDSStreamDetailAudio();
      break;
    case CStreamDetail::SUBTITLE:
    {
      if (m_bHasSubsFilter)
        infos = new CDSStreamDetailSubfilter(INTERNAL);
      else
        infos = new CDSStreamDetailSubtitle();
      break;
    }
    case CStreamDetail::EDITION:
      m_mkveditions = true;
    case CStreamDetail::BD_TITLE:

      infos = new CDSStreamDetailEdition();	

      break;
    default: continue;
    }

    CDSStreamDetail& pS = dynamic_cast<CDSStreamDetail&> (*infos);

    pS.IAMStreamSelect_Index = i;

    g_charsetConverter.wToUTF8(wname, pS.displayname);
    CoTaskMemFree(wname);

    pS.flags = flags; pS.lcid = lcid; pS.group = group; pS.pObj = (IPin *)pObj; pS.pUnk = (IPin *)pUnk;
    if (flags & AMSTREAMSELECTINFO_ENABLED)
      pS.connected = true;

    if (!m_mkveditions)
      MediaTypeToStreamDetail(mediaType, (CStreamDetail&)(*infos));

    if (group == CStreamDetail::AUDIO)
    {
      /* Audio stream */
      if (pS.displayname.find("Undetermined") != std::string::npos)
        pS.displayname = StringUtils::Format("A: Audio %02d", i + 1);

      m_audioStreams.push_back(static_cast<CDSStreamDetailAudio *>(infos));
      CLog::Log(LOGINFO, "{} Audio stream found : {} - index: {}", __FUNCTION__, pS.displayname.c_str(), pS.IAMStreamSelect_Index);
    }
    else if (group == CStreamDetail::SUBTITLE)
    {
      if (m_bHasSubsFilter)
        m_subfilterStreams.push_back(static_cast<CDSStreamDetailSubfilter *>(infos));
      else
        SubtitleManager->GetSubtitles().push_back(static_cast<CDSStreamDetailSubtitle*>(infos));
      CLog::Log(LOGINFO, "{} Subtitle stream found : {} - index: {}", __FUNCTION__, pS.displayname.c_str(), pS.IAMStreamSelect_Index);
    }
    else if (group == CStreamDetail::EDITION || group == CStreamDetail::BD_TITLE)
    {
      CDSStreamDetailEdition * pEdition = static_cast<CDSStreamDetailEdition *>(infos);
      if (Com::SComQIPtr<IBdStreamSelect> pBDSS = m_pSplitter.p)
      {
        pBDSS->GetTitleInfo(m_editionStreams.size(), NULL, &pEdition->m_rtDuration);
      }
      m_editionStreams.push_back(pEdition);
      CLog::Log(LOGINFO, "{} Editions stream found : {} - index : {}", __FUNCTION__, pS.displayname.c_str(), pS.IAMStreamSelect_Index);
    }

    DeleteMediaType(mediaType);
  }
}

void CStreamsManager::LoadStreamsInternal()
{
  // Enumerate output pins
  PIN_DIRECTION dir;
  int i = 0, j = 0;
  std::string pinName, guid = "";
  CStreamDetail*infos = NULL;
  bool audioPinAlreadyConnected = false;//, subtitlePinAlreadyConnected = FALSE;

  // If we're playing a DVD, the bottom method doesn't work
  if (CGraphFilters::Get()->IsDVD())
  { // The playback need to be started in order to get informations
    return;
  }

  int nIn = 0, nOut = 0, nInC = 0, nOutC = 0;
  CountPins(m_pSplitter, nIn, nOut, nInC, nOutC);
  CLog::Log(LOGDEBUG, "{} The splitter has %d output pins", __FUNCTION__, nOut);

  BeginEnumPins(m_pSplitter, pEP, pPin)
  {
    if (SUCCEEDED(pPin->QueryDirection(&dir)) && (dir == PINDIR_OUTPUT))
    {
      g_charsetConverter.wToUTF8(GetPinName(pPin), pinName);
      CLog::Log(LOGDEBUG, "{} Output pin found : {}", __FUNCTION__, pinName.c_str());

      BeginEnumMediaTypes(pPin, pET, pMediaType)
      {

        g_charsetConverter.wToUTF8(StringFromGUID(pMediaType->majortype), guid);
        CLog::Log(LOGDEBUG, "{} \tOutput pin major type : {} {}", __FUNCTION__, GuidNames[pMediaType->majortype], guid.c_str());

        g_charsetConverter.wToUTF8(StringFromGUID(pMediaType->subtype), guid);
        CLog::Log(LOGDEBUG, "{} \tOutput pin sub type : {} {}", __FUNCTION__, GuidNames[pMediaType->subtype], guid.c_str());

        g_charsetConverter.wToUTF8(StringFromGUID(pMediaType->formattype), guid);
        CLog::Log(LOGDEBUG, "{} \tOutput pin format type : {} {}", __FUNCTION__, GuidNames[pMediaType->formattype], guid.c_str());

        if (pMediaType->majortype == MEDIATYPE_Video)
          infos = &m_videoStream;
        else if (pMediaType->majortype == MEDIATYPE_Audio)
          infos = new CDSStreamDetailAudio();
        else if (pMediaType->majortype == MEDIATYPE_Subtitle)
          infos = new CDSStreamDetailSubtitle();
        else
          continue;

        CDSStreamDetail& pS = dynamic_cast<CDSStreamDetail&>(*infos);

        if (pS.displayname.empty())
          pS.displayname = pinName;

        MediaTypeToStreamDetail(pMediaType, *infos);

        pS.pObj = pPin;
        pPin->ConnectedTo(&pS.pUnk);

        if (pMediaType->majortype == MEDIATYPE_Audio)
        {
          if (pS.displayname.find("Undetermined") != std::string::npos)
            pS.displayname = StringUtils::Format("Audio %02d", i + 1);

          if (i == 0)
            pS.flags = AMSTREAMSELECTINFO_ENABLED;
          if (pS.pUnk)
            pS.connected = true;

          m_audioStreams.push_back(static_cast<CDSStreamDetailAudio *>(infos));
          CLog::Log(LOGINFO, "{} Audio stream found : {}", __FUNCTION__, pS.displayname.c_str());
          i++;

        }
        else if (pMediaType->majortype == MEDIATYPE_Subtitle)
        {
          if (pS.displayname.find("Undetermined") != std::string::npos)
            pS.displayname = StringUtils::Format("Subtitle %02d", j + 1);

          if (j == 0)
            pS.flags = AMSTREAMSELECTINFO_ENABLED;
          if (pS.pUnk)
            pS.connected = true;

          SubtitleManager->GetSubtitles().push_back(static_cast<CDSStreamDetailSubtitle *>(infos));
          CLog::Log(LOGINFO, "{} Subtitle stream found : {}", __FUNCTION__, pS.displayname.c_str());
          j++;
        }
        else if (pMediaType->majortype == MEDIATYPE_Video)
        {

        }

        break; // if the pin has multiple output type, get only the first one
      }
      EndEnumMediaTypes(pMediaType)
    }
  }
  EndEnumPins
}

void CStreamsManager::LoadStreams(const std::string& file)
{
  if (!m_init)
    return;

  m_currentFile = file.empty() ? g_application.CurrentFile() : file;

  std::string splitterName;
  g_charsetConverter.wToUTF8(GetFilterName(m_pSplitter), splitterName);

  CLog::Log(LOGDEBUG, "{} Looking for streams in {} splitter", __FUNCTION__, splitterName.c_str());

  // Does the splitter support IAMStreamSelect?
  m_pIAMStreamSelect = NULL;
  HRESULT hr = m_pSplitter->QueryInterface(__uuidof(m_pIAMStreamSelect), (void **)&m_pIAMStreamSelect);
  if (SUCCEEDED(hr) && !(CGraphFilters::Get()->UsingMediaPortalTsReader()))
  {
    CLog::Log(LOGDEBUG, "{} Get IAMStreamSelect interface from {}", __FUNCTION__, splitterName.c_str());
    LoadIAMStreamSelectStreamsInternal();
  }
  else
    LoadStreamsInternal();

  if (m_bHasSubsFilter)
  {
    std::string subName;
    g_charsetConverter.wToUTF8(GetFilterName(m_pSubs), subName);
    m_pIAMStreamSelectSub = NULL;

    HRESULT hr = m_pSubs->QueryInterface(__uuidof(m_pIAMStreamSelectSub), (void **)&m_pIAMStreamSelectSub);
    if (SUCCEEDED(hr))
    {
      CLog::Log(LOGDEBUG, "{} Get IAMStreamSelect interface from {}", __FUNCTION__, subName.c_str());

      HRESULT hr = m_pSubs->QueryInterface(__uuidof(m_pIDirectVobSub), (void **)&m_pIDirectVobSub);
      if (SUCCEEDED(hr))
        m_bIsXYVSFilter = true;

      m_subfilterStreams_int = m_subfilterStreams;

      // The filter is asked to stop hunting for subtitle files of its own accord, and
      // LoadExternalSubtitles below hands it every file instead. It hunts by looking beside
      // whatever the graph's source filter says it is playing, which for a disc folder is
      // BDMV/index.bdmv or VIDEO_TS/VIDEO_TS.IFO -- so it goes looking for "index.srt", and
      // no disc has one. Doing the finding here reaches the disc's own folder, uses Kodi's
      // own scan rather than a second one beside it, and leaves the order of what was loaded
      // known, which is what lets each track be named after the file it came from.
      //
      // It has already hunted once by this point, at its own loading, and that is harmless:
      // handing it a file it already holds is not a second copy, which was measured rather
      // than assumed -- three files beside a video, already found and handed over again, and
      // the filter went on offering three.
      m_pIDirectVobSub->put_LoadSettings(1, false, false, true);
      m_InitialSubsDelay = GetSubTitleDelay();

      Com::SComQIPtr<IDSPlayerCustom> pDPlayerCustom;
      hr = m_pSubs->QueryInterface(__uuidof(pDPlayerCustom), (void **)&pDPlayerCustom);
      if (SUCCEEDED(hr))
      {
        pDPlayerCustom->SetPropertyPageCallback(CDSPropertyPage::PropertyPageCallback);
      }

      SubInterface(ADD_EXTERNAL_SUB);
    }
  }

  SubtitleManager->Initialize();

  if (!SubtitleManager->Ready())
  {
    if (!m_bHasSubsFilter)
    {
      // Nothing in the graph can draw a subtitle: no subtitle filter, and the internal
      // manager could not be created either. The streams collected above are dropped rather
      // than offering tracks that would never show up.
      CLog::Log(LOGWARNING,
                "{} No subtitle renderer available, subtitles are disabled for this file. Add a "
                "subtitle filter such as XySubFilter to the matching rule in mediasconfig.xml "
                "to enable subtitle support", __FUNCTION__);
      SubtitleManager->Unload();
      return;
    }

    // A subtitle filter is in the graph, and the internal manager stands down deliberately
    // when one is -- "disabled libsubs xyfilter loaded" is it saying so. It is not
    // implemented for madVR anyway; XySubFilter draws through madVR's own subtitle interface
    // instead, which is the whole reason it is there.
    //
    // Treating that as "no subtitle renderer" threw away every track the splitter had just
    // found, on Blu-ray and DVD alike: a disc whose subtitles LAV listed one line earlier
    // offered none at all. Only the external-subtitle handling below needs the internal
    // manager, and with a filter present that is done by SubInterface(ADD_EXTERNAL_SUB).
    CLog::Log(LOGDEBUG, "{} Subtitles are drawn by the subtitle filter rather than internally; "
                        "{} track(s) from the splitter stand", __FUNCTION__,
              m_subfilterStreams.size());

    // Subtitle files sitting beside what is being played. This used to be reached only
    // through the internal manager below, which is never ready with a subtitle filter
    // loaded -- so with XySubFilter in the graph, which is every graph, nothing here ever
    // looked for one. A disc felt that hardest: its own tracks are offered and a file next
    // to it was not, though it is the same list either way.
    LoadExternalSubtitles();
    return;
  }

  /* We're done, internal audio & subtitles stream are loaded.
     We load external subtitle file */

  std::vector<std::string> subtitles;
  CDSFile::ScanForSubtitles(m_currentFile, subtitles);

  for (std::vector<std::string>::const_iterator it = subtitles.begin(); it != subtitles.end(); ++it)
  {
    SubtitleManager->AddSubtitle(*it);
  }
  CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleCached = true;
  SubtitleManager->SelectBestSubtitle();

  SubtitleManager->SetSubtitleVisible(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleOn);
}

void CStreamsManager::LoadExternalSubtitles()
{
  if (!m_bIsXYVSFilter)
    return;

  std::vector<std::string> subtitles;
  CDSFile::ScanForSubtitles(m_currentFile, subtitles);

  if (subtitles.empty())
    return;

  // Longest name first, so the shortest is handed over last. put_FileName does not add one
  // subtitle: it loads every subtitle whose file name begins the same way, and it *replaces*
  // what the filter was holding rather than adding to it -- handing over "Film.en.srt" and
  // then "Film.pt.srt" leaves the filter offering one track, not two. The shortest name is
  // the one whose base covers the most, so going last it picks the others up with it:
  // "Film.srt" last brings "Film.en.srt" and "Film.pt.srt" back with it.
  std::sort(subtitles.begin(), subtitles.end(),
            [](const std::string& a, const std::string& b) { return a.size() > b.size(); });

  const int before = GetSubtitleCount();
  for (const auto& subtitle : subtitles)
    AddSubtitle(subtitle);

  // Which is a best effort and not a guarantee: subtitle files beside a disc need not share
  // a name at all, and any the filter dropped are said so here rather than quietly missing.
  if (GetExternalSubtitleCount() != static_cast<int>(subtitles.size()))
    CLog::Log(LOGWARNING,
              "{} - the subtitle filter lists {} file(s) where {} were handed to it; it loads "
              "by shared name, so files named differently from each other cannot all be held",
              __FUNCTION__, GetExternalSubtitleCount(), subtitles.size());

  // Worth stating plainly, because the two numbers answering each other is the whole check:
  // how many files were found against how many tracks the filter went on to offer.
  CLog::Log(LOGINFO,
            "{} - handed {} subtitle file(s) to the subtitle filter, which now offers {} "
            "track(s) where it offered {}",
            __FUNCTION__, subtitles.size(), GetSubtitleCount(), before);
}

int CStreamsManager::GetExternalSubtitleCount()
{
  if (!m_bHasSubsFilter)
    return 0;

  int found = 0;
  for (const auto& subtitle : m_subfilterStreams)
  {
    if (subtitle->m_subType == EXTERNAL)
      found++;
  }
  return found;
}

int CStreamsManager::ExternalSubtitle(int nth)
{
  if (!m_bHasSubsFilter || nth < 0)
    return -1;

  int found = 0;
  int index = 0;
  for (const auto& subtitle : m_subfilterStreams)
  {
    if (subtitle->m_subType == EXTERNAL && found++ == nth)
      return index;
    index++;
  }
  return -1;
}

int CStreamsManager::SubtitleIsExternal(int iStream)
{
  if (!m_bHasSubsFilter || iStream < 0 || iStream >= static_cast<int>(m_subfilterStreams.size()))
    return -1;

  if (m_subfilterStreams[iStream]->m_subType != EXTERNAL)
    return -1;

  int found = 0;
  for (int i = 0; i < iStream; i++)
  {
    if (m_subfilterStreams[i]->m_subType == EXTERNAL)
      found++;
  }
  return found;
}

int CStreamsManager::GetSubtitle()
{
  if (!m_bHasSubsFilter)
  {
    return SubtitleManager->GetSubtitle();
  }

  if (m_subfilterStreams.size() == 0)
    return -1;

  int i = 0;
  for (std::vector<CDSStreamDetailSubfilter *>::const_iterator it = m_subfilterStreams.begin();
    it != m_subfilterStreams.end(); ++it, i++)
  {
    if ((*it)->flags & AMSTREAMSELECTINFO_ENABLED)
      return i;
  }
  return -1;
}

int CStreamsManager::GetSubtitleCount()
{
  if (!m_bHasSubsFilter)
  {
    return SubtitleManager->GetSubtitleCount();
  }

  return m_subfilterStreams.size();
}

void CStreamsManager::GetSubtitleInfo(int iStream, SubtitleStreamInfo& strStreamName)
{
  if (!m_bHasSubsFilter)
  {
    std::string strStreamLang;
    //if -1 its current stream which mean we get the stream which as the used flag
    SubtitleManager->GetSubtitleName(iStream, strStreamName.name, strStreamLang);
    // The ISO language is needed by the GUI to resolve a readable language, otherwise
    // every subtitle ends up labelled "Unknown"
    strStreamName.language = strStreamLang;
    return;
  }


  if (m_subfilterStreams.size() == 0)
    return;

  int i = 0;
  for (std::vector<CDSStreamDetailSubfilter *>::const_iterator it = m_subfilterStreams.begin();
    it != m_subfilterStreams.end(); ++it, i++)
  {
    if (i == iStream)
    {
      strStreamName.name = (*it)->displayname;
      strStreamName.language = (*it)->isolang;
      
    }
  }
}


bool CStreamsManager::GetSubtitleVisible()
{
  if (!m_bHasSubsFilter)
  {
    return SubtitleManager->GetSubtitleVisible();
  }

  return m_bSubfilterVisible;
}

void CStreamsManager::SetSubtitleVisible(bool bVisible)
{
  if (!m_bHasSubsFilter)
  {
    SubtitleManager->SetSubtitleVisible(bVisible);
    return;
  }

  CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleOn = bVisible;
  ShowSubtitleFilter(bVisible);
}

void CStreamsManager::ShowSubtitleFilter(bool bVisible)
{
  m_bSubfilterVisible = bVisible;

  if (!m_bIsXYVSFilter)
    return;

  m_pIDirectVobSub->put_HideSubtitles(!m_bSubfilterVisible);
}

void CStreamsManager::resetDelayInterface()
{
  int iValueAudio = m_InitialAudioDelay * 1000;
  BOOL bValue = iValueAudio != 0;
  int iValueSubs = m_InitialSubsDelay * 1000;

  if (m_bIsXYVSFilter)
    m_pIDirectVobSub->put_SubtitleTiming(iValueSubs, 1000, 1000);

  if (m_bIsLavAudio)
    m_pILAVAudioSettings->SetAudioDelay(bValue, iValueAudio);
}

void CStreamsManager::SetAudioInterface()
{
  Com::SComPtr<IBaseFilter> pAudio;
  pAudio = CGraphFilters::Get()->Audio.pBF;

  if (!pAudio)
    return;

  std::string audioName;
  HRESULT hraudio;
  g_charsetConverter.wToUTF8(GetFilterName(pAudio), audioName);

  hraudio = pAudio->QueryInterface(__uuidof(m_pILAVAudioSettings), (void **)&m_pILAVAudioSettings);
  if (SUCCEEDED(hraudio))
  {
    CLog::Log(LOGDEBUG, "{} Get LAVAudio Settings interface from {}", __FUNCTION__, audioName.c_str());
    m_bIsLavAudio = true;
  }

  m_InitialAudioDelay = GetAVDelay();
}

DWORD CStreamsManager::GetHWAccel()
{
  Com::SComQIPtr<ILAVVideoSettings> pLAVVideoSettings = CGraphFilters::Get()->Video.pBF.p;
  if (!pLAVVideoSettings)
    return 0;

  return pLAVVideoSettings->GetHWAccel();
}

void CStreamsManager::SetAVDelay(float fValue, int iDisplayerLatency)
{
  //delay float secs to int msecs
  int iValue = round(fValue * 1000.0f);

  //invert the sign displaylatency because kodi interface
  iDisplayerLatency = -iDisplayerLatency;
  iValue = iValue + iDisplayerLatency;

  //get delay and invert the sign because kodi interface
  iValue = -iValue;
  BOOL bValue = iValue != 0;

  if (m_bIsLavAudio)
  {
    HRESULT hr = m_pILAVAudioSettings->SetAudioDelay(bValue, iValue);
  }

  m_lastDelay = iValue;
}

float CStreamsManager::GetAVDelay()
{
  float fValue = 0.0f;
  int iValue;
  BOOL bValue;

  if (!m_bIsLavAudio && !m_bIsFFDSAudio)
    return 0;

  if (m_bIsLavAudio)
  {

    m_pILAVAudioSettings->GetAudioDelay(&bValue, &iValue);
    if (bValue > 10)
    {
      
    }
  }

  fValue = (float)iValue / 1000.0f;

  return -fValue;
}

void CStreamsManager::SetSubTitleDelay(float fValue)
{
  if (!m_bHasSubsFilter)
  {
    SubtitleManager->SetSubtitleDelay(fValue);
    return;
  }

  int iValue = round(fValue * 1000.0f);

  //get delay and invert the sign because kodi interface
  iValue = -iValue;

  if (m_bIsXYVSFilter)
    m_pIDirectVobSub->put_SubtitleTiming(iValue, 1000, 1000);
}

float CStreamsManager::GetSubTitleDelay()
{
  if (!m_bHasSubsFilter)
  {
    return SubtitleManager->GetSubtitleDelay();
  }

  float fValue = 0.0f;
  int iValue, a, b;

  if (!m_bIsXYVSFilter)
    return 0;

  if (m_bIsXYVSFilter)
    m_pIDirectVobSub->get_SubtitleTiming(&iValue, &a, &b);

  fValue = (float)iValue / 1000.0f;
  return fValue;
}

int CStreamsManager::AddSubtitle(const std::string& subFilePath)
{
  if (!m_bHasSubsFilter)
  {
    return SubtitleManager->AddSubtitle(subFilePath);
  }

  if (!m_bIsXYVSFilter)
    return -1;

  std::wstring subFileW;
  std::string subFile;

  subFile = CDSFile::SmbToUncPath(subFilePath);

  if (StringUtils::StartsWithNoCase(subFile, "special://"))
    subFile = CSpecialProtocol::TranslatePath(subFile);

  g_charsetConverter.utf8ToW(subFile, subFileW);

  CLog::Log(LOGDEBUG, "{} Successfully loaded external subtitle name  \"{}\" ", __FUNCTION__, subFile.c_str());

  m_pIDirectVobSub->put_FileName(const_cast<wchar_t*>(subFileW.c_str()));

  // The list is built again from the filter's own answer rather than appended to, so the
  // entries made for the externals it listed last time are this function's to free: nothing
  // else holds them, and m_subfilterStreams_int owns only the splitter's own tracks.
  for (auto& subtitle : m_subfilterStreams)
  {
    if (subtitle->m_subType == EXTERNAL &&
        std::find(m_subfilterStreams_int.begin(), m_subfilterStreams_int.end(), subtitle) ==
            m_subfilterStreams_int.end())
      delete subtitle;
  }

  m_subfilterStreams = m_subfilterStreams_int;

  SubInterface(ADD_EXTERNAL_SUB);

  return GetSubtitleCount() - 1;
}

bool CStreamsManager::SetSubtitle(const std::string &sTrackName)
{
  if (!m_init || sTrackName.empty())
    return false;

  CSingleExit lock(m_lock);

  if (GetSubtitleCount() <= 0)
    return false;

  int iStream = 0;
  for (const auto &sub : m_subfilterStreams)
  {
    if (sub->displayname == sTrackName)
    {
      SetSubtitle(iStream);
      return true;
    }
    iStream++;
  }

  return false;
}

void CStreamsManager::SetSubtitle(int iStream)
{
  if (!m_bHasSubsFilter)
  {
    SubtitleManager->SetSubtitle(iStream);
    m_iCurrentSubtitleStream = iStream;
    return;
  }

  if (!m_init)
    return;

  CSingleExit lock(m_lock);

  if (GetSubtitleCount() <= 0)
    return;

  if (iStream > GetSubtitleCount()-1)
    return;

  long disableIndex = GetSubtitle(), enableIndex = iStream;

  m_readyEvent.Reset();
  XbmcThreads::CAutoSetEvent event(&m_readyEvent);

  CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleStream = enableIndex;

  if (m_subfilterStreams[enableIndex]->m_subType == EXTERNAL)
  {    
    CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleExtTrackName = m_subfilterStreams[enableIndex]->displayname;
    
    DisconnectCurrentSubtitlePins();

    CDSStreamDetailSubfilter *s = reinterpret_cast<CDSStreamDetailSubfilter *>(m_subfilterStreams[enableIndex]);

    m_pIAMStreamSelectSub->Enable(s->IAMStreamSelect_Index, AMSTREAMSELECTENABLE_ENABLE);
    s->flags = AMSTREAMSELECTINFO_ENABLED; // for gui
    s->connected = true;
  }

  else if (m_pIAMStreamSelect)
  {
    CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleExtTrackName = "";

    if (disableIndex >= 0 && m_subfilterStreams[disableIndex]->connected)
      DisconnectCurrentSubtitlePins();

    SubInterface(SELECT_INTERNAL_SUB);

    if (SUCCEEDED(m_pIAMStreamSelect->Enable(m_subfilterStreams[enableIndex]->IAMStreamSelect_Index, AMSTREAMSELECTENABLE_ENABLE)))
    {
      m_subfilterStreams[enableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
      m_subfilterStreams[enableIndex]->connected = true;
    }
  }
  // "%i" is not a placeholder to fmt, so this line said "number %i" for years and never once
  // said which stream. The same mistake as the extrafilter%i and the audio channel count.
  CLog::Log(LOGDEBUG, "{} Successfully selected subfilter stream number {}", __FUNCTION__,
            enableIndex);
  SetSubtitleVisible(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleOn);
}

void CStreamsManager::SubInterface(SelectSubType action)
{
  if (!m_bIsXYVSFilter)
    return;

  DWORD nCount;
  DWORD group;
  if (FAILED(m_pIAMStreamSelectSub->Count(&nCount))) { nCount = 0; }
  for (DWORD i = 0; i < nCount; i++)
  {
    WCHAR* pszName = nullptr;
    if (SUCCEEDED(m_pIAMStreamSelectSub->Info(i, nullptr, nullptr, nullptr, &group, &pszName, nullptr, nullptr)))
    {
      std::string fileName;
      std::string langName;
      g_charsetConverter.wToUTF8(pszName, fileName);
      if (action == ADD_EXTERNAL_SUB)
      {
        if (group == XYVSFILTER_SUB_EXTERNAL)
        {
          std::unique_ptr<CDSStreamDetailSubfilter> s(new CDSStreamDetailSubfilter(EXTERNAL));

          // The filter names an external track after whatever is left of its file name once
          // the *video's* name is taken off the front -- "en" for "Film.en.srt" beside
          // "Film.mkv" -- which is a language when the two sit side by side under one name
          // and is not one otherwise. Beside an unpacked disc it is never one: what is
          // playing is called index.bdmv or VIDEO_TS.IFO, nothing is named after that, so
          // nothing is taken off and the whole of "Charlie Brown.pt" is left. And for a file
          // with no suffix at all there is nothing left over, where the filter falls back to
          // repeating the video's own name.
          //
          // So the name is put through the same parser Kodi uses for every other external
          // subtitle, against the disc's own folder rather than the index file inside it.
          // "en" stays English, "Charlie Brown.pt" becomes Portuguese, and a file with
          // nothing to say about itself becomes Unknown rather than the film's title.
          //
          // Deliberately not matched up with the files that were handed over, which would be
          // the obvious thing and is wrong: put_FileName does not add one subtitle, it loads
          // every subtitle sharing that file's base name, and the filter lists the result in
          // its own order. There is no nth file to line up with the nth track.
          const std::string filterName = fileName;
          const ExternalStreamInfo external = CUtil::GetExternalStreamDetailsFromFilename(
              CDSFile::SubtitleNameBase(m_currentFile), fileName + ".srt");
          fileName = external.language;

          langName = ISOToLanguage(fileName);
          if (!langName.empty() && !fileName.empty())
            s->displayname = StringUtils::Format("{} ({})", langName.c_str(), fileName.c_str());
          else if (!fileName.empty())
            s->displayname = fileName;
          else
            s->displayname = "Unknown";

          s->displayname += " " + g_localizeStrings.Get(21602);

          s->isolang = fileName;
          s->IAMStreamSelect_Index = i;
          m_subfilterStreams.push_back(s.release());
          CLog::Log(LOGINFO, "{} - subtitle file track {}, which the filter calls \"{}\", is \"{}\"",
                    __FUNCTION__, i, filterName.c_str(),
                    m_subfilterStreams.back()->displayname.c_str());
        }
      }
      if (action == SELECT_INTERNAL_SUB)
      {
        if (group == XYVSFILTER_SUB_INTERNAL)
          m_pIAMStreamSelectSub->Enable(i, AMSTREAMSELECTENABLE_ENABLE);
      }
    }
    CoTaskMemFree(pszName);
  }
}

void CStreamsManager::SelectBestAudio()
{
  std::string sPrefCodec = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_PREFAUDIOCODEC);
  int iLibrary = CMediaSettings::GetInstance().GetCurrentVideoSettings().m_AudioStream;
  if ((iLibrary < GetAudioStreamCount()) && !(iLibrary < 0))
  {
    SetAudioStream(iLibrary);
  }
  else if (!m_audioStreams.empty() && !sPrefCodec.empty())
  {
    std::vector<std::string> codecs = StringUtils::Split(sPrefCodec, "|");

    std::string sPrefLang = g_langInfo.GetLocale().GetLanguageCode();

    int i = 0;
    for (const auto &it : m_audioStreams)
    {     
      wchar_t buffer[64];
      std::string audioLang;
      std::string audioCodec = it->m_strCodecName;
      StringUtils::ToLower(audioCodec);

      if (it->lcid)
      {
        int len = 0;
        if (len = GetLocaleInfo(it->lcid, LOCALE_SISO639LANGNAME, buffer, 64))
        {
          audioLang = WToA(buffer);
          audioLang.resize(len - 1); //get rid of last \0
        }
        
      }
      
      for (const auto &codec : codecs)
      {
        if (audioCodec.find(codec) != std::string::npos && audioLang == sPrefLang)
        {
          SetAudioStream(i);
          return;
        }
      }

      i++;
    }
  }
}

void CStreamsManager::SelectBestSubtitle(const std::string &fileName /* ="" */)
{
  if (!m_bHasSubsFilter)
  {
    SubtitleManager->SelectBestSubtitle();
    return;
  }

  int selectFirst = -1;
  int select = -1;
  int iLibrary;
  std::string exSubTrackName = "";

  CDSPlayerDatabase db;
  if (db.Open())
  {
    exSubTrackName = db.GetSubtitleExtTrackName(fileName);
    db.Close();
    if (!exSubTrackName.empty())
    {
      if (SetSubtitle(exSubTrackName))
        return;
    }
  }

  // set previuos selected stream
  iLibrary = CMediaSettings::GetInstance().GetCurrentVideoSettings().m_SubtitleStream;
  
  if ((iLibrary < CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>()->GetSubtitleCount()) && !(iLibrary < 0))
    select = iLibrary;
    
  // set prefered external sub or first external sub
  else if (!m_subfilterStreams.empty())
  {
    int i = 0;
    for (std::vector<CDSStreamDetailSubfilter *>::const_iterator it = m_subfilterStreams.begin();
      it != m_subfilterStreams.end(); ++it, i++)
    {
      if ((*it)->m_subType & EXTERNAL)
      {
        if (selectFirst == -1)
          selectFirst = i;

        if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_EXSUBTITLELANGUAGE) != "origina")
        {

          std::string sPref;
          if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_EXSUBTITLELANGUAGE) == "default")
          {
            sPref = g_langInfo.GetLocale().GetLanguageCode();
            sPref = ProbeLangForLanguage(sPref.c_str());
          }
          else
            sPref = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_EXSUBTITLELANGUAGE);

          std::string sName = ProbeLangForLanguage((*it)->isolang.c_str());
          if (StringUtils::EqualsNoCase(sName, sPref))
          {
            select = i;
            break;
          }
        }
      }
      if (select == -1)
        select = selectFirst;
    }
  }

  if (select != -1)
    SetSubtitle(select);
}

void CStreamsManager::DisconnectCurrentSubtitlePins()
{
  int i = GetSubtitle();
  if (i == -1)
    return;

  m_subfilterStreams[i]->connected = false;
  m_subfilterStreams[i]->flags = 0;
}


bool CStreamsManager::InitManager()
{
  if (!g_dsGraph)
    return false;

  m_pSplitter = CGraphFilters::Get()->Splitter.pBF;  
  m_pSubs = CGraphFilters::Get()->Subs.pBF;
  m_pGraphBuilder = g_dsGraph->pFilterGraph;

  // Create subtitle manager
  SubtitleManager.reset(new CSubtitleManager(this));

  m_bHasSubsFilter = m_pSubs != nullptr;
  m_init = true;
  m_bIsXYVSFilter = false;
  m_bIsLavAudio = false;
  m_bIsFFDSAudio = false;
  m_InitialAudioDelay = 0.0f;
  m_InitialSubsDelay = 0.0f;

  return true;
}

bool CStreamsManager::Create()
{
  if (CDSPlayer::PlayerState == DSPLAYER_CLOSING || CDSPlayer::PlayerState == DSPLAYER_CLOSED)
    return false;

  if ((m_pSingleton = new CStreamsManager()))
    return true;

  return false;
}

void CStreamsManager::Destroy()
{
  SAFE_DELETE(m_pSingleton);
}

void CStreamsManager::WaitUntilReady()
{
  // If the event has not been Reset(), Wait() will return immediately
  m_readyEvent.Wait();
}

int CStreamsManager::GetChannels()
{
  int i = GetAudioStream();
  return (i == -1) ? 0 : m_audioStreams[i]->m_iChannels;
}

int CStreamsManager::GetChannels(int istream)
{
  return (istream == -1) ? 0 : m_audioStreams[istream]->m_iChannels;
}

int CStreamsManager::GetBitsPerSample(int istream)
{
  return (istream == -1) ? 0 : m_audioStreams[istream]->m_iBitRate;
}

int CStreamsManager::GetSampleRate(int istream)
{
  return (istream == -1) ? 0 : m_audioStreams[istream]->m_iSampleRate;
}

void CStreamsManager::ExtractCodecDetail(CStreamDetail& s, std::string& codecInfos)
{
  if (s.m_eType == CStreamDetail::SUBTITLE)
    return;

  std::vector<std::string> tokens;
  StringUtils::Tokenize(codecInfos, tokens, "|");

  if (tokens.empty() || tokens.size() != 2)
  {
    switch (s.m_eType)
    {
    case CStreamDetail::AUDIO:
    {
      CDSStreamDetailAudio& pS = reinterpret_cast<CDSStreamDetailAudio &>(s);
      pS.m_strCodec = pS.m_strCodecName = codecInfos;
      return;
    }
    case CStreamDetail::VIDEO:
    {
      CDSStreamDetailVideo& pS = reinterpret_cast<CDSStreamDetailVideo &>(s);
      pS.m_strCodec = pS.m_strCodecName = codecInfos;
      return;
    }
    }
  }

  switch (s.m_eType)
  {
  case CStreamDetail::AUDIO:
  {
    CDSStreamDetailAudio& pS = reinterpret_cast<CDSStreamDetailAudio &>(s);
    pS.m_strCodecName = tokens[0];
    pS.m_strCodec = tokens[1];
    return;
  }
  case CStreamDetail::VIDEO:
  {
    CDSStreamDetailVideo& pS = reinterpret_cast<CDSStreamDetailVideo &>(s);
    pS.m_strCodecName = tokens[0];
    pS.m_strCodec = tokens[1];
    return;
  }
  }
}

void CStreamsManager::MediaTypeToStreamDetail(AM_MEDIA_TYPE *pMediaType, CStreamDetail& s)
{
  std::string codecname;
  if (pMediaType->majortype == MEDIATYPE_Audio)
  {
    CDSStreamDetailAudio& infos = static_cast<CDSStreamDetailAudio&>(s);
    
    if (pMediaType->formattype == FORMAT_WaveFormatEx)
    {
      if (pMediaType->cbFormat >= sizeof(WAVEFORMATEX))
      {
        WAVEFORMATEX *f = reinterpret_cast<WAVEFORMATEX *>(pMediaType->pbFormat);
        infos.m_iChannels = f->nChannels;
        infos.m_iSampleRate = f->nSamplesPerSec;
        infos.m_iBitRate = f->wBitsPerSample;
        codecname = CMediaTypeEx::GetAudioCodecName(pMediaType->subtype, f->wFormatTag);
        ExtractCodecDetail(infos, codecname);
      }
    }
    else if (pMediaType->formattype == FORMAT_VorbisFormat2)
    {
      if (pMediaType->cbFormat >= sizeof(VORBISFORMAT2))
      {
        VORBISFORMAT2 *v = reinterpret_cast<VORBISFORMAT2 *>(pMediaType->pbFormat);
        infos.m_iChannels = v->Channels;
        infos.m_iSampleRate = v->SamplesPerSec;
        infos.m_iBitRate = v->BitsPerSample;
        codecname = CMediaTypeEx::GetAudioCodecName(pMediaType->subtype, 0);
        ExtractCodecDetail(infos, codecname);
      }
    }
    else if (pMediaType->formattype == FORMAT_VorbisFormat)
    {
      if (pMediaType->cbFormat >= sizeof(VORBISFORMAT))
      {
        VORBISFORMAT *v = reinterpret_cast<VORBISFORMAT *>(pMediaType->pbFormat);
        infos.m_iChannels = v->nChannels;
        infos.m_iSampleRate = v->nSamplesPerSec;
        infos.m_iBitRate = v->nAvgBitsPerSec;
        codecname = CMediaTypeEx::GetAudioCodecName(pMediaType->subtype, 0);
        ExtractCodecDetail(infos, codecname);
      }
    }
  }
  else if (pMediaType->majortype == MEDIATYPE_Video)
  {
    CDSStreamDetailVideo& infos = static_cast<CDSStreamDetailVideo&>(s);

    // GET STEREO FLAG FROM KODI
    CFileItem fileItem = g_application.CurrentFileItem();
    if (fileItem.HasVideoInfoTag() && fileItem.GetVideoInfoTag()->HasStreamDetails())
    {
      infos.m_strStereoMode = fileItem.GetVideoInfoTag()->m_streamDetails.GetStereoMode();
    }
    if (infos.m_strStereoMode.empty())
      infos.m_strStereoMode = CServiceBroker::GetGUI()->GetStereoscopicsManager().DetectStereoModeByString(g_application.CurrentFileItem().GetPath());

    if (pMediaType->formattype == FORMAT_VideoInfo)
    {
      if (pMediaType->cbFormat >= sizeof(VIDEOINFOHEADER))
      {
        VIDEOINFOHEADER *v = reinterpret_cast<VIDEOINFOHEADER *>(pMediaType->pbFormat);
        infos.m_iWidth = v->bmiHeader.biWidth;
        infos.m_iHeight = v->bmiHeader.biHeight;
        v->AvgTimePerFrame > 0 ? infos.m_fps = (10000000.0 / v->AvgTimePerFrame) : infos.m_fps = 0.0f;
        codecname = CMediaTypeEx::GetVideoCodecName(pMediaType->subtype, v->bmiHeader.biCompression, &infos.m_iFourcc);
        ExtractCodecDetail(infos, codecname);
      }
    }
    else if (pMediaType->formattype == FORMAT_MPEG2Video)
    {
      if (pMediaType->cbFormat >= (sizeof(MPEG2VIDEOINFO) - 4))
      {
        MPEG2VIDEOINFO *m = reinterpret_cast<MPEG2VIDEOINFO *>(pMediaType->pbFormat);
        infos.m_iWidth = m->hdr.bmiHeader.biWidth;
        infos.m_iHeight = m->hdr.bmiHeader.biHeight;
        m->hdr.AvgTimePerFrame > 0 ? infos.m_fps = (10000000.0 / m->hdr.AvgTimePerFrame) : infos.m_fps = 0.0f;
        codecname = CMediaTypeEx::GetVideoCodecName(pMediaType->subtype, m->hdr.bmiHeader.biCompression, &infos.m_iFourcc);
        ExtractCodecDetail(infos, codecname);
        if (infos.m_iFourcc == 0)
        {
          infos.m_iFourcc = 'MPG2';
          infos.m_strCodecName = "MPEG2 Video";
          infos.m_strCodec = "mpeg2";
        }
      }
    }
    else if (pMediaType->formattype == FORMAT_VideoInfo2)
    {
      if (pMediaType->cbFormat >= sizeof(VIDEOINFOHEADER2))
      {
        VIDEOINFOHEADER2 *v = reinterpret_cast<VIDEOINFOHEADER2 *>(pMediaType->pbFormat);
        infos.m_iWidth = v->bmiHeader.biWidth;
        infos.m_iHeight = v->bmiHeader.biHeight;
        v->AvgTimePerFrame > 0 ? infos.m_fps = (10000000.0 / v->AvgTimePerFrame) : infos.m_fps = 0.0f;
        codecname = CMediaTypeEx::GetVideoCodecName(pMediaType->subtype, v->bmiHeader.biCompression, &infos.m_iFourcc);
        ExtractCodecDetail(infos, codecname);
      }
    }
    else if (pMediaType->formattype == FORMAT_MPEGVideo)
    {
      if (pMediaType->cbFormat >= sizeof(MPEG1VIDEOINFO))
      {
        MPEG1VIDEOINFO *m = reinterpret_cast<MPEG1VIDEOINFO *>(pMediaType->pbFormat);
        infos.m_iWidth = m->hdr.bmiHeader.biWidth;
        infos.m_iHeight = m->hdr.bmiHeader.biHeight;
        m->hdr.AvgTimePerFrame > 0 ? infos.m_fps = (10000000.0 / m->hdr.AvgTimePerFrame) : infos.m_fps = 0.0f;
        codecname = CMediaTypeEx::GetVideoCodecName(pMediaType->subtype, m->hdr.bmiHeader.biCompression, &infos.m_iFourcc);
        ExtractCodecDetail(infos, codecname);
      }
    }
    infos.m_fAspect = (float)infos.m_iWidth / infos.m_iHeight;

    // fourcc infos
    CLog::Log(LOGINFO, "{} \tVideo stream fourcc : {}{}{}{}", __FUNCTION__, infos.m_iFourcc >> 24 & 0xff, infos.m_iFourcc >> 16 & 0xff, infos.m_iFourcc >> 8 & 0xff, infos.m_iFourcc & 0xff);

    // TODO: That's really bad. We determine if we *can* use dxva with the file, and not if
    // dxva will be used. There're also many others parameters that need to be take into
    // account. Unfortunatly, DirectShow does not provide any usefull informations about that.
    // TODO: Rename function and XML parameter to something more understandable (like
    // dxva_capable)

    if (infos.m_iFourcc == 'H264' || infos.m_iFourcc == 'AVC1' || infos.m_iFourcc == 'WVC1'
      || infos.m_iFourcc == 'WMV3' || infos.m_iFourcc == 'WMVA' || infos.m_iFourcc == 'CCV1')
      CGraphFilters::Get()->SetIsUsingDXVADecoder(true);

    // CCV1 is a fake fourcc code generated by Haali in order to trick the Microsoft h264
    // decoder included in Windows 7. ffdshow handle that fourcc well, but it may not be
    // the case for others decoders
    if (infos.m_iFourcc == 'CCV1')
    {
      CLog::Log(LOGINFO, "Haali Media Splitter is configured for using a fake fourcc code 'CCV1'. If you've got an error message, be sure your video decoder handle that fourcc well.");
    }
  }
  else if (pMediaType->majortype == MEDIATYPE_Subtitle)
  {
    CDSStreamDetailSubtitle& infos = static_cast<CDSStreamDetailSubtitle&>(s);

    if (pMediaType->formattype == FORMAT_SubtitleInfo
      && pMediaType->cbFormat >= sizeof(SUBTITLEINFO))
    {
      SUBTITLEINFO *i = reinterpret_cast<SUBTITLEINFO *>(pMediaType->pbFormat);
      infos.isolang = i->IsoLang;
      infos.offset = i->dwOffset;
      g_charsetConverter.wToUTF8(i->TrackName, infos.trackname);
      if (!infos.lcid)
        infos.lcid = ISO6392ToLcid(i->IsoLang);
    }

    infos.subtype = pMediaType->subtype;
  }
#if TODO
  if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SHOWSPLITTERDETAIL) ||
      CGraphFilters::Get()->UsingMediaPortalTsReader())
    FormatStreamName(s);
  else
    FormatStreamNameBySplitter(s);
#endif
}

int CStreamsManager::GetPictureWidth()
{
  return m_videoStream.m_iWidth;
}

int CStreamsManager::GetPictureHeight()
{
  return m_videoStream.m_iHeight;
}

std::string CStreamsManager::GetAudioCodecName(int istream)
{
  if (m_audioStreams.size() == 0 || istream == -1)
    return "";

  return m_audioStreams[istream]->m_strCodec;
}

std::string CStreamsManager::GetAudioCodecDisplayName()
{ 
  int i = GetAudioStream(); return (i == -1) ? "" : m_audioStreams[i]->m_strCodecName; 
}
std::string CStreamsManager::GetAudioCodecDisplayName(int istream)
{ 
  if (m_audioStreams.size() == 0 || istream == -1)
    return "";

  return m_audioStreams[istream]->m_strCodecName; 
}

std::string CStreamsManager::GetVideoCodecName()
{
  return m_videoStream.m_strCodec;
}

std::string CStreamsManager::GetStereoMode()
{
  return m_videoStream.m_strStereoMode;
}

CDSStreamDetailVideo * CStreamsManager::GetVideoStreamDetail(unsigned int iIndex /*= 0*/)
{
  /* We currently supports only one video stream */
  if (iIndex != 0)
    return NULL;

  return &m_videoStream;
}

CDSStreamDetailAudio * CStreamsManager::GetAudioStreamDetail(unsigned int iIndex /*= 0*/)
{
  if (iIndex > m_audioStreams.size())
    return NULL;

  return m_audioStreams[iIndex];
}

void CStreamsManager::FormatStreamNameBySplitter(CStreamDetail& s)
{
  CDSStreamDetail& pS = dynamic_cast<CDSStreamDetail&> (s);

  StringUtils::Replace(pS.displayname, "A:","");
  StringUtils::Replace(pS.displayname, "V:", "");
  StringUtils::Replace(pS.displayname, "S:", "");
  StringUtils::Replace(pS.displayname, "E:", "");

  StringUtils::Replace(pS.displayname, "Audio -", "");
  StringUtils::Replace(pS.displayname, "Video -", "");
  StringUtils::Replace(pS.displayname, "Subtitle -", "");
  StringUtils::Replace(pS.displayname, "Edition -", "");
  
  

  CRegExp regExp;
  regExp.RegComp(".*(\\[.*\\])");
  if (regExp.RegFind(pS.displayname, 0) != -1)
    StringUtils::TrimRight(pS.displayname, regExp.GetMatch(1).c_str());

  StringUtils::Trim(pS.displayname);
}

void CStreamsManager::FormatStreamName(CStreamDetail& s)
{
  std::vector<std::shared_ptr<CRegExp>> regex;

  std::shared_ptr<CRegExp> reg(new CRegExp(true));
  reg->RegComp("(.*?)(\\(audio.*?\\)|\\(subtitle.*?\\))"); // mkv source audio / subtitle
  regex.push_back(reg);

  reg.reset(new CRegExp(true));
  reg->RegComp(".* - (.*),.*\\(.*\\)"); // mpeg source audio / subtitle
  regex.push_back(reg);

  CDSStreamDetail& pS = dynamic_cast<CDSStreamDetail&> (s);

  for (std::vector<std::shared_ptr<CRegExp>>::iterator
    it = regex.begin(); it != regex.end(); ++it)
  {
    if ((*it)->RegFind(pS.displayname) > -1)
    {
      pS.displayname = (*it)->GetMatch(1);
      break;
    }
  }

  regex.clear();

  // First, if lcid isn't 0, try GetLocalInfo
  if (pS.lcid)
  {
    wchar_t buffer[64];
    std::wstring _name; int len = 0;
    if (len = GetLocaleInfoW(pS.lcid, LOCALE_SLANGUAGE, buffer, 64))
    {
      _name = buffer;
      _name.resize(len - 1); //get rid of last \0
      std::string name; g_charsetConverter.wToUTF8(_name, name);
      if (s.m_eType == CStreamDetail::SUBTITLE && !((CDSStreamDetailSubtitle&)pS).trackname.empty())
        pS.displayname = StringUtils::Format("{} ({})", name.c_str(), ((CDSStreamDetailSubtitle&)pS).trackname.c_str());
      else
        pS.displayname = name;
    }
  }

  if (s.m_eType == CStreamDetail::SUBTITLE && pS.displayname.empty())
  {
    CDSStreamDetailSubtitle& c = (CDSStreamDetailSubtitle&)pS;
    std::string name = ISOToLanguage(c.isolang);
    if (!name.empty() && !c.trackname.empty())
      c.displayname = StringUtils::Format("{} ({})", name.c_str(), ((CDSStreamDetailSubtitle&)pS).trackname.c_str());
    else if (!c.trackname.empty())
      c.displayname = c.trackname;
    else
      c.displayname = "Unknown";
  }
}

CSubtitleManager::CSubtitleManager(CStreamsManager* pStreamManager) :
 m_pStreamManager(pStreamManager), m_rtSubtitleDelay(0)
{
  memset(&m_subtitleMediaType, 0, sizeof(AM_MEDIA_TYPE));
  m_subtitleMediaType.majortype = MEDIATYPE_Subtitle;

  m_bSubtitlesVisible = CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleOn;
  SetSubtitleDelay(CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleDelay);

  m_pManager.reset();
}

CSubtitleManager::~CSubtitleManager()
{
  Unload();
}

#define FONT_STYLE_NORMAL       0
#define FONT_STYLE_BOLD         1
#define FONT_STYLE_ITALICS      2
#define FONT_STYLE_BOLD_ITALICS 3

static unsigned int color[8] = { 0x0000FFFF, 0x00FFFFFF, 0x00FF9900, 0x0000FF00, 0x00FFCC, 0x00FFFF00, 0x00E5E5E5, 0x00C0C0C0 };

void CSubtitleManager::Initialize()
{
  // Initialize subtitles
  // 1. Create Subtitle Manager

  SIZE s; s.cx = 0; s.cy = 0;
  ISubManager *pManager = NULL;

  if (CGraphFilters::Get()->Subs.pBF != nullptr) {
    CLog::Log(LOGINFO, "{} disabled libsubs xyfilter loaded", __FUNCTION__);
    return;
  }

  CLog::Log(LOGINFO, "{} enabled libsubs.dl", __FUNCTION__);
  // Log manager for the DLL
  m_Log.reset(new ILogImpl());
  bool rescreate;
  if (StringUtils::ToLower(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_VIDEORENDERER)) == "madvr")
  {
    CLog::Log(LOGERROR, "for madvr use xyfilter");
    //rescreate = CreateD3D9SubtitleManager(CGraphFilters::Get()->GetD3DDevice(), s, m_Log.get(), g_dsSettings.pRendererSettings->subtitlesSettings, &pManager);
  }
  else
    rescreate = CreateD3D11SubtitleManager(DX::DeviceResources().Get()->GetD3DDevice(), s, m_Log.get(), g_dsSettings.pRendererSettings->subtitlesSettings, &pManager);

    

  if (!pManager)
    return;
  
  //m_pManager.reset(pManager, std::bind2nd(std::not_fn(DeleteSubtitleManager), m_dll));
  m_pManager.reset(pManager);

  SSubStyle style; //auto default on constructor

  // Build style based on XBMC settings
  
  style.alpha[0] = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("subtitles.opacity");
  //CHANGED FROM RES_PAL_4x3
  CServiceBroker::GetWinSystem()->GetGfxContext().SetScalingResolution(RES_CUSTOM, true);
  style.fontSize = (double)(CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("subtitles.fontsize")) * 50.0 / 96.0;

  //Convert string FFFFFFFF to COLORREF
  std::string hexColor = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_SUBTITLES_COLOR);
  std::sscanf(hexColor.c_str(), "%x", &style.colors[0]);


  int fontStyle = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_SUBTITLES_STYLE);
  switch (fontStyle)
  {
  case FONT_STYLE_NORMAL:
  default:
    style.fItalic = false;
    style.fontWeight = FW_NORMAL;
    break;
  case FONT_STYLE_BOLD:
    style.fItalic = false;
    style.fontWeight = FW_BOLD;
    break;
  case FONT_STYLE_ITALICS:
    style.fontWeight = FW_NORMAL;
    style.fItalic = true;
    break;
  case FONT_STYLE_BOLD_ITALICS:
    style.fItalic = true;
    style.fontWeight = FW_BOLD;
    break;
  }

  style.charSet = CDSCharsetConverter::getCharsetIdByName(g_langInfo.GetSubtitleCharSet());

  style.borderStyle = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("subtitles.border");
  style.shadowDepthX = style.shadowDepthY = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("subtitles.shadowdepth");
  style.outlineWidthX = style.outlineWidthY = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("subtitles.outlinewidth");

  std::wstring fontName;
  g_charsetConverter.utf8ToW(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString("subtitles.fontname"), fontName);
  if (fontName == L"DEFAULT")
  {
    fontName = L"Arial";
  }
  style.fontName = (wchar_t *)CoTaskMemAlloc(fontName.length() * sizeof(wchar_t) + 2);
  if (style.fontName)
    wcscpy_s(style.fontName, fontName.length() + 1, fontName.c_str());

  bool override = CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_SUBTITLES_OVERRIDEASSFONTS);
  m_pManager->SetStyle(&style, override);

  if (FAILED(m_pManager->InsertPassThruFilter(g_dsGraph->pFilterGraph)))
  {
    // No internal subs. May have some externals!
  }

  m_pManager->SetEnable(true);
}

bool CSubtitleManager::Ready()
{
  return (!!m_pManager);
}

HRESULT CSubtitleManager::AlphaBlt(ID3D11DeviceContext1* pDevContext, Com::SmartRect& pSrc, Com::SmartRect& pDest, Com::SmartRect& renderRect)
{

  if (m_pManager)
  {
    m_pManager->SetDeviceContext(pDevContext);
    return m_pManager->AlphaBlt(pSrc, pDest, renderRect);
  }
  return E_FAIL;
}

void CSubtitleManager::StopThread()
{
  if (!m_subtitleStreams.empty() && m_pManager)
    m_pManager->StopThread();
}

void CSubtitleManager::StartThread()
{
  if (!m_subtitleStreams.empty() && m_pManager)
    m_pManager->StartThread(DX::DeviceResources::Get()->GetD3DDevice());
}

void CSubtitleManager::Unload()
{
  if (m_pManager)
  {
    m_pManager->SetSubPicProvider(NULL);
    m_pManager->Free();
  }
  while (!m_subtitleStreams.empty())
  {
    if (m_subtitleStreams.back()->m_subType == EXTERNAL)
      ((CDSStreamDetailSubtitleExternal *)m_subtitleStreams.back())->substream.FullRelease();
    delete m_subtitleStreams.back();
    m_subtitleStreams.pop_back();
  }
  m_pManager.reset();
}

void CSubtitleManager::SetTimePerFrame(REFERENCE_TIME iTimePerFrame)
{
  if (m_pManager)
    m_pManager->SetTimePerFrame(iTimePerFrame);
}
void CSubtitleManager::SetTime(REFERENCE_TIME rtNow)
{
  if (m_pManager)
    m_pManager->SetTime(rtNow - m_rtSubtitleDelay);
}

void CSubtitleManager::SetSizes(Com::SmartRect window, Com::SmartRect video)
{
  if (m_pManager)
    m_pManager->SetSizes(window, video);
}

std::vector<CDSStreamDetailSubtitle *>& CSubtitleManager::GetSubtitles()
{
  CLog::Log(LOGINFO, "{}: requesting vector subtitles {}", __FUNCTION__, m_subtitleStreams.size());
  return m_subtitleStreams;
}

int CSubtitleManager::GetSubtitleCount()
{
  CLog::Log(LOGINFO, "{}: count {}", __FUNCTION__, m_subtitleStreams.size());
  return m_subtitleStreams.size();
}

int CSubtitleManager::GetSubtitle()
{
  if (m_subtitleStreams.size() == 0)
    return -1;

  int i = 0;
  for (std::vector<CDSStreamDetailSubtitle *>::const_iterator it = m_subtitleStreams.begin();
    it != m_subtitleStreams.end(); ++it, i++)
  {
    if ((*it)->flags & AMSTREAMSELECTINFO_ENABLED)
    {
      CLog::Log(LOGINFO, "{}: GetSubtitle {}", __FUNCTION__,i);
      return i;
    }
  }
  CLog::Log(LOGINFO, "{}: no subtitle stream selected returning -1", __FUNCTION__);
  return -1;
}

void CSubtitleManager::GetSubtitleName(int iStream, std::string &strStreamName, std::string& strStreamLang)
{
  if (m_subtitleStreams.size() == 0)
    return;

  int i = 0;
  for (std::vector<CDSStreamDetailSubtitle *>::const_iterator it = m_subtitleStreams.begin();
    it != m_subtitleStreams.end(); ++it, i++)
  {
    if (i == iStream)
    {
      strStreamName = (*it)->displayname;
      strStreamLang = (*it)->isolang;
    }
  }
  CLog::Log(LOGINFO, "{}: stream name {} isolang {}", __FUNCTION__, strStreamName.c_str(), strStreamLang.c_str());
}

void CSubtitleManager::SetSubtitle(int iStream)
{
  CLog::Log(LOGINFO, "{}: changing subtitle stream to {}", __FUNCTION__, iStream);
  if (CGraphFilters::Get()->IsDVD())
    return; // currently not implemented

  CSingleExit lock(m_pStreamManager->m_lock);

  //If no subtitles just return
  if (GetSubtitleCount() <= 0)
    return;

  if (iStream > GetSubtitleCount())
    return;

  long disableIndex = GetSubtitle(), enableIndex = iStream;

  m_pStreamManager->m_readyEvent.Reset();
  XbmcThreads::CAutoSetEvent event(&m_pStreamManager->m_readyEvent);

  bool stopped = false;
  std::string subtitlePath = "";
  Com::SComPtr<IPin> newAudioStreamPin;

  CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleStream = enableIndex;

  if (m_subtitleStreams[enableIndex]->m_subType == EXTERNAL)
  {
    /* External subtitle */
    DisconnectCurrentSubtitlePins();

    CDSStreamDetailSubtitleExternal *s = reinterpret_cast<CDSStreamDetailSubtitleExternal *>(m_subtitleStreams[enableIndex]);

    m_pManager->SetSubPicProvider(s->substream);

    s->flags = AMSTREAMSELECTINFO_ENABLED; // for gui
    s->connected = true;

    SetSubtitleVisible(CMediaSettings::GetInstance().GetAtStartVideoSettings().m_SubtitleOn);
    return;
  }

  if (m_pStreamManager->m_pIAMStreamSelect)
  {
    if (disableIndex >= 0 && m_subtitleStreams[disableIndex]->connected)
      DisconnectCurrentSubtitlePins();

    if (SUCCEEDED(m_pStreamManager->m_pIAMStreamSelect->Enable(m_subtitleStreams[enableIndex]->IAMStreamSelect_Index, AMSTREAMSELECTENABLE_ENABLE)))
    {
      m_pManager->SetSubPicProviderToInternal();
      m_subtitleStreams[enableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
      m_subtitleStreams[enableIndex]->connected = true;
      SetSubtitleVisible(CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleOn);
      CLog::Log(LOGDEBUG, "{} Successfully selected subtitle stream", __FUNCTION__);
    }
  }
  else
  {

    Com::SComPtr<IPin> connectedToPin = NULL;
    Com::SComPtr<IPin> oldAudioStreamPin = NULL;
    HRESULT hr = S_OK;

    g_dsGraph->Stop(); // Current position is kept by the graph
    stopped = true;

    /* Disconnect pins */
    if (m_subtitleStreams[disableIndex]->m_subType != EXTERNAL)
    {

      oldAudioStreamPin = m_subtitleStreams[disableIndex]->pObj;
      hr = oldAudioStreamPin->ConnectedTo(&connectedToPin);

      if (connectedToPin)
        hr = m_pStreamManager->m_pGraphBuilder->Disconnect(connectedToPin);

      m_pStreamManager->m_pGraphBuilder->Disconnect(oldAudioStreamPin);
    }

    m_subtitleStreams[disableIndex]->flags = 0;
    m_subtitleStreams[disableIndex]->connected = false;

    if (!connectedToPin)
    {
      m_subtitleMediaType.subtype = m_subtitleStreams[enableIndex]->subtype;
      connectedToPin = GetFirstSubtitlePin(); // Find where connect the subtitle
    }

    if (!connectedToPin)
    {
      /* Nothing support subtitles! */
      CLog::Log(LOGERROR, "{} You're trying to use subtitle, but no filters in the graph seems to accept subtitles!", __FUNCTION__);
      hr = E_FAIL;
      goto done;
    }

    /* Enable filter */
    newAudioStreamPin = m_subtitleStreams[enableIndex]->pObj;

    hr = m_pStreamManager->m_pGraphBuilder->ConnectDirect(newAudioStreamPin, connectedToPin, NULL);
    if (FAILED(hr))
    {
      /* Reconnect previous working pins */
      if (m_subtitleStreams[disableIndex]->m_subType == INTERNAL)
        m_pStreamManager->m_pGraphBuilder->ConnectDirect(oldAudioStreamPin, connectedToPin, NULL);

      m_subtitleStreams[disableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
      m_subtitleStreams[disableIndex]->connected = true;

      goto done;
    }

    m_subtitleStreams[enableIndex]->flags = AMSTREAMSELECTINFO_ENABLED;
    m_subtitleStreams[enableIndex]->connected = true;

    m_pManager->SetSubPicProviderToInternal();

  done:
    if (stopped)
      g_dsGraph->Play();

    if (m_bSubtitlesVisible)
    {
      if (SUCCEEDED(hr))
        CLog::Log(LOGINFO, "{} Successfully changed subtitle stream", __FUNCTION__);
      else
        CLog::Log(LOGERROR, "{} Can't change subtitle stream", __FUNCTION__);
    }
  }
}

bool CSubtitleManager::GetSubtitleVisible()
{
  return m_bSubtitlesVisible;
}

void CSubtitleManager::SetSubtitleVisible(bool bVisible)
{
  CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleOn = bVisible;
  m_bSubtitlesVisible = bVisible;
  if (m_pManager)
    m_pManager->SetEnable(bVisible);
}

int CSubtitleManager::AddSubtitle(const std::string& subFilePath)
{
  if (!m_pManager)
    return -1;

  std::unique_ptr<CDSStreamDetailSubtitleExternal> s(new CDSStreamDetailSubtitleExternal());

  if (m_subtitleStreams.empty())
    s->flags = AMSTREAMSELECTINFO_ENABLED;

  s->path = CSpecialProtocol::TranslatePath(subFilePath);
  if (!XFILE::CFile::Exists(s->path))
    return -1;

  // Try to detect isolang of subtitle
  CRegExp regex(true);
  regex.RegComp("^.*\\.(.*)\\.[^\\.]*.*$");
  if (regex.RegFind(s->path) > -1)
    s->isolang = regex.GetMatch(1);

  if (!s->isolang.empty())
  {
    s->lcid = ISO6392ToLcid(s->isolang.c_str());
    if (!s->lcid)
      s->lcid = ISO6391ToLcid(s->isolang.c_str());

    m_pStreamManager->FormatStreamName(*(s.get()));
  }

  if (s->displayname.empty())
    s->displayname = URIUtils::GetFileName(s->path);
  else
    s->displayname += " [External]";

  // Load subtitle file
  Com::SComPtr<ISubStream> pSubStream;

  std::wstring unicodePath; 
  g_charsetConverter.utf8ToW(s->path, unicodePath);

  if (SUCCEEDED(m_pManager->LoadExternalSubtitle(unicodePath.c_str(), &pSubStream)))
  {
    s->substream = pSubStream;
    // if the sub is a vob, it has an internal title, grab it
    wchar_t* title = NULL;
    if (SUCCEEDED(m_pManager->GetStreamTitle(pSubStream, &title)))
    {
      g_charsetConverter.wToUTF8(std::wstring(title), s->displayname);
      s->displayname += " [External]";
      CoTaskMemFree(title);
    }

    CLog::Log(LOGINFO, "{} Successfully loaded subtitle file \"{}\"", __FUNCTION__, s->path.c_str());
    m_subtitleStreams.push_back(s.release());
    return m_subtitleStreams.size() - 1;
  }

  return -1;
}

void CSubtitleManager::DisconnectCurrentSubtitlePins(void)
{
  int i = GetSubtitle();
  if (i == -1)
    return;

  if (m_pStreamManager->m_pIAMStreamSelect)
  {
    m_subtitleStreams[i]->connected = false;
    m_subtitleStreams[i]->flags = 0;
  }
  else
  {
    if (m_subtitleStreams[i]->connected && m_subtitleStreams[i]->m_subType == INTERNAL)
    {
      Com::SComPtr<IPin> pin = NULL;

      if (VFW_E_NOT_CONNECTED == m_subtitleStreams[i]->pObj->ConnectedTo(&pin))
      {
        // Shouldn't happened!
        m_subtitleStreams[i]->connected = false;
        m_subtitleStreams[i]->flags = 0;
        return;
      }

      g_dsGraph->Stop();
      m_pStreamManager->m_pGraphBuilder->Disconnect(m_subtitleStreams[i]->pObj); // Splitter's output pin
      m_pStreamManager->m_pGraphBuilder->Disconnect(pin); // Filter's input pin
      g_dsGraph->Play();
    }
    m_subtitleStreams[i]->connected = false;
    m_subtitleStreams[i]->flags = 0;
  }
  //m_bSubtitlesUnconnected = true;
}

void CSubtitleManager::SetSubtitleDelay(float fValue)
{
  m_rtSubtitleDelay = (int)(-fValue * 10000000i64); // second to directshow timebase
}

float CSubtitleManager::GetSubtitleDelay(void)
{
  return (float)m_rtSubtitleDelay / 10000000i64;
}

IPin *CSubtitleManager::GetFirstSubtitlePin(void)
{
  PIN_DIRECTION  pindir;

  BeginEnumFilters(g_dsGraph->pFilterGraph, pEF, pBF)
  {
    BeginEnumPins(pBF, pEP, pPin)
    {
      Com::SComPtr<IPin> pFellow;

      if (SUCCEEDED(pPin->QueryDirection(&pindir)) &&
        pindir == PINDIR_INPUT && pPin->ConnectedTo(&pFellow) == VFW_E_NOT_CONNECTED)
      {
        if (pPin->QueryAccept(&m_subtitleMediaType) == S_OK)
          return (pPin);
      }
    }
    EndEnumPins
  }
  EndEnumFilters

    return NULL;
}

std::string CStreamsManager::ISOToLanguage(std::string code)
{
  if (code.length() == 2)
    return ISO6391ToLanguage(code.c_str());
  else
    return ISO6392ToLanguage(code.c_str());
}

void CStreamsManager::LoadDVDStreams()
{
  Com::SComPtr<IDvdInfo2> pDvdI = CGraphFilters::Get()->DVD.dvdInfo;
  Com::SComPtr<IDvdControl2> pDvdC = CGraphFilters::Get()->DVD.dvdControl;

  DVD_VideoAttributes vid;
  // First, video
  HRESULT hr = pDvdI->GetCurrentVideoAttributes(&vid);
  if (SUCCEEDED(hr))
  {
    m_videoStream.m_iWidth = vid.ulSourceResolutionX;
    m_videoStream.m_iHeight = vid.ulSourceResolutionY;
    switch (vid.Compression)
    {
    case DVD_VideoCompression_MPEG1:
      m_videoStream.m_strCodecName = "MPEG1";
      break;
    case DVD_VideoCompression_MPEG2:
      m_videoStream.m_strCodecName = "MPEG2";
      break;
    default:
      m_videoStream.m_strCodecName = "Unknown";
    }
  }

  UpdateDVDStream();

}

void CStreamsManager::UpdateDVDStream()
{
  if (m_dvdStreamLoaded)
    return;

  // Second, audio
  unsigned long nbrStreams = 0, currentStream = 0;
  HRESULT hr = CGraphFilters::Get()->DVD.dvdInfo->GetCurrentAudio(&nbrStreams, &currentStream);
  if (FAILED(hr))
    return;

  CSingleExit lock(m_lock);

  m_audioStreams.clear();
  for (unsigned int i = 0; i < nbrStreams; i++)
  {
    std::unique_ptr<CDSStreamDetailAudio> s(new CDSStreamDetailAudio());
    DVD_AudioAttributes audio;
    hr = CGraphFilters::Get()->DVD.dvdInfo->GetAudioAttributes(i, &audio);

    s->m_iChannels = audio.bNumberOfChannels;
    s->m_iSampleRate = audio.dwFrequency;
    s->m_iBitRate = 0;
    s->lcid = audio.Language;
    if (i == currentStream)
    {
      s->flags = AMSTREAMSELECTINFO_ENABLED;
      s->connected = true;
    }
    switch (audio.AudioFormat)
    {
    case DVD_AudioFormat_AC3:
      s->m_strCodecName = "Dolby AC3";
      break;
    case DVD_AudioFormat_MPEG1:
      s->m_strCodecName = "MPEG1";
      break;
    case DVD_AudioFormat_MPEG1_DRC:
      s->m_strCodecName = "MPEG1 (DCR)";
      break;
    case DVD_AudioFormat_MPEG2:
      s->m_strCodecName = "MPEG2";
      break;
    case DVD_AudioFormat_MPEG2_DRC:
      s->m_strCodecName = "MPEG2 (DCR)";
      break;
    case DVD_AudioFormat_LPCM:
      s->m_strCodecName = "LPCM";
      break;
    case DVD_AudioFormat_DTS:
      s->m_strCodecName = "DTS";
      break;
    case DVD_AudioFormat_SDDS:
      s->m_strCodecName = "SDDS";
      break;
    default:
      s->m_strCodecName = "Unknown";
      break;
    }
    FormatStreamName(*(s.get()));

    m_audioStreams.push_back(s.release());
  }

  BOOL isDisabled = false;
  hr = CGraphFilters::Get()->DVD.dvdInfo->GetCurrentSubpicture(&nbrStreams, &currentStream, &isDisabled);
  if (FAILED(hr))
    return;

  for (unsigned int i = 0; i < nbrStreams; i++)
  {
    DVD_SubpictureAttributes subpic;
    hr = CGraphFilters::Get()->DVD.dvdInfo->GetSubpictureAttributes(i, &subpic);
    if (subpic.Type != DVD_SPType_Language)
      continue;

    std::unique_ptr<CDSStreamDetailSubtitle> s(new CDSStreamDetailSubtitle());
    std::unique_ptr<CDSStreamDetailSubfilter> s2(new CDSStreamDetailSubfilter(INTERNAL));
    s->lcid = subpic.Language;
    s2->lcid = subpic.Language;
    FormatStreamName(*(s.get()));
    FormatStreamName(*(s2.get()));
    SubtitleManager->GetSubtitles().push_back(s.release());
    m_subfilterStreams.push_back(s2.release());
  }

  m_dvdStreamLoaded = true;
}
CDSStreamDetailSubtitle * CSubtitleManager::GetSubtitleStreamDetail(unsigned int iIndex /*= 0*/)
{
  if (iIndex > m_subtitleStreams.size())
    return NULL;

  if (m_subtitleStreams[iIndex]->m_subType != INTERNAL)
    return NULL;

  return m_subtitleStreams[iIndex];
}

CDSStreamDetailSubtitleExternal* CSubtitleManager::GetExternalSubtitleStreamDetail(unsigned int iIndex /*= 0*/)
{
  if (iIndex > m_subtitleStreams.size())
    return NULL;

  if (m_subtitleStreams[iIndex]->m_subType != EXTERNAL)
    return NULL;

  return reinterpret_cast<CDSStreamDetailSubtitleExternal *>(m_subtitleStreams[iIndex]);
}

void CSubtitleManager::InternalDeleteSubtitleManager(ISubManager* pManager)
{
  
  if (!DeleteSubtitleManager(pManager))
  {
  }
}

void CSubtitleManager::SelectBestSubtitle()
{
  int select = -1;
  int iLibrary;

  // set previuos selected stream
  iLibrary = CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleStream;
  if ((iLibrary < g_application.GetComponent<CApplicationPlayer>()->GetSubtitleCount()) && !(iLibrary < 0))
    select = iLibrary;

  // set first external sub
  else if (m_subtitleStreams.size() != 0)
  {
    int i = 0;
    for (std::vector<CDSStreamDetailSubtitle *>::const_iterator it = m_subtitleStreams.begin();
      it != m_subtitleStreams.end(); ++it, i++)
    {
      if ((*it)->m_subType & EXTERNAL)
      {
        select = i;
        break;
      }
    }
  }
  
  if (select != -1)
    SetSubtitle(select);
  else
  {
    //the connection is already made so test it
    int selectinternal = GetSubtitle();
    if (selectinternal > -1)
      SetSubtitle(selectinternal);
  }

  SetSubtitleVisible(CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleOn);
}

#endif
