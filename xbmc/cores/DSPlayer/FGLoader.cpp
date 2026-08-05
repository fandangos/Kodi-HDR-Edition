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

#include "FGLoader.h"
#include "DSBlurayProbe.h"
#include "Filters/DSBluraySource.h"
#include "Filters/DSDvdSource.h"
#include "DSPlayer.h"
#include "Filters/RendererSettings.h"
#include "PixelShaderList.h"
#include "streamsmanager.h"
#include "DSUtil/DSUtil.h"
#include "SComCli.h"

#include "utils/charsetconverter.h"
#include "utils/Log.h"
#include "dialogs/GUIDialogOK.h"
#include "guilib/GUIWindowManager.h"
#include "filtercorefactory/filtercorefactory.h"
#include "profiles/ProfileManager.h"
#include "settings/Settings.h"
#include "utils/SystemInfo.h"

#include "filters/XBMCFileSource.h"
#include "Filters/madVRAllocatorPresenter.h"

#include "Utils/AudioEnumerator.h"
#include "Utils/DSFilterEnumerator.h"
#include "DVDFileInfo.h"
#include "video/VideoInfoTag.h"
#include "utils/URIUtils.h"
#include "utils/DSFileUtils.h"
#include "Filters/Sanear/Factory.h"
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "guilib/GUIComponent.h"
#include "Filters/MPCVideoRenderer/VideoRenderer.h"
#include "video/VideoFileItemClassify.h"
#include <filesystem/SpecialProtocol.h>
#pragma comment(lib, "sanear.lib")
#pragma comment(lib, "soxr.lib")
#pragma comment(lib, "SoundTouch.lib")
#pragma comment(lib, "bs2b.lib")
#pragma comment(lib, "atls.lib")
namespace
{
  std::vector<std::pair<std::string, std::string>> GetDevices()
  {
    std::vector<std::pair<std::string, std::string>> ret;

    Com::SComPtr<IMMDeviceEnumerator> enumerator;
    Com::SComPtr<IMMDeviceCollection> collection;
    UINT count = 0;

    if (SUCCEEDED(enumerator.CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER)) &&
      SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, &collection)) &&
      SUCCEEDED(collection->GetCount(&count))) {

      for (UINT i = 0; i < count; i++) {
        LPWSTR id = nullptr;
        Com::SComPtr<IMMDevice> device;
        Com::SComPtr<IPropertyStore> devicePropertyStore;
        PROPVARIANT friendlyName;
        PropVariantInit(&friendlyName);

        if (SUCCEEDED(collection->Item(i, &device)) &&
          SUCCEEDED(device->GetId(&id)) &&
          SUCCEEDED(device->OpenPropertyStore(STGM_READ, &devicePropertyStore)) &&
          SUCCEEDED(devicePropertyStore->GetValue(PKEY_Device_FriendlyName, &friendlyName))) {

          std::string sFriendlyName;
          std::string sId;
          g_charsetConverter.wToUTF8(friendlyName.pwszVal, sFriendlyName);
          g_charsetConverter.wToUTF8(static_cast<LPWSTR>(id), sId);

          ret.emplace_back(sFriendlyName, sId);
          PropVariantClear(&friendlyName);
          CoTaskMemFree(id);
        }
      }
    }

    return ret;
  }
}

using namespace std;

CFGLoader::CFGLoader()
  :m_pFGF(NULL)
  , m_bIsAutoRender(false)
{
}

CFGLoader::~CFGLoader()
{
  // CSingleExit(*this) removed: it released this object's own lock without holding it and
  // then re-acquired it an instant before the lock itself is destroyed - on the reopen of
  // a navigated disc that acquire never returned

  CFilterCoreFactory::Destroy();
  SAFE_DELETE(m_pFGF);

  CLog::Log(LOGDEBUG, "{} Ressources released", __FUNCTION__);
}

HRESULT CFGLoader::InsertSourceFilter(CFileItem& pFileItem, const std::string& filterName)
{

  HRESULT hr = E_FAIL;

  /* XBMC SOURCE FILTER  */
  /*if (CUtil::IsInArchive(pFileItem.GetPath()))
  {
  CLog::Log(LOGINFO,"{} File \"{}\" need a custom source filter", __FUNCTION__, pFileItem.GetPath().c_str());
  CXBMCAsyncStream* pXBMCStream = new CXBMCAsyncStream(pFileItem.GetPath(), &CGraphFilters::Get()->Source.pBF, &hr);
  if (SUCCEEDED(hr))
  {
  hr = g_dsGraph->pFilterGraph->AddFilter(CGraphFilters::Get()->Source.pBF, L"XBMC Source Filter");
  if (FAILED(hr))
  {
  CLog::Log(LOGERROR, "{} Failed to add xbmc source filter to the graph", __FUNCTION__);
  return hr;
  }
  CGraphFilters::Get()->Source.osdname = "XBMC File Source";
  CGraphFilters::Get()->Source.pData = (void *) pXBMCStream;
  CGraphFilters::Get()->Source.isinternal = true;
  CLog::Log(LOGINFO, "{} Successfully added xbmc source filter to the graph", __FUNCTION__);
  }
  return hr;
  }*/
  /* DVD NAVIGATOR */

  // Only when the rule really chose Microsoft's DVD Navigator. This branch demands
  // IDvdControl2 of whatever filter it is handed and gives up with E_NOINTERFACE when it is
  // not there -- so with the rule pointed at our own libdvdnav source instead, every
  // VIDEO_TS.IFO failed at "Failed to insert the source filter" before the filter was ever
  // asked to load anything. IsDVDFile() is true of any .ifo, which is not the same question.
  if (KODI::VIDEO::IsDVDFile(pFileItem) && filterName != INTERNAL_DVD_SOURCE)
  {
    std::string path = pFileItem.GetPath();
    if (StringUtils::EqualsNoCase(StringUtils::Left(path, 6), "smb://"))
    {
      StringUtils::Replace(path, "smb://", "//");
      pFileItem.SetPath(path);
    }

    hr = InsertFilter(filterName, CGraphFilters::Get()->Splitter);
    if (SUCCEEDED(hr))
    {
      if (!((CGraphFilters::Get()->DVD.dvdControl = CGraphFilters::Get()->Splitter.pBF) && (CGraphFilters::Get()->DVD.dvdInfo = CGraphFilters::Get()->Splitter.pBF)))
      {
        CGraphFilters::Get()->DVD.Clear();
        return E_NOINTERFACE;
      }
    }

    CGraphFilters::Get()->SetIsDVD(true);
    std::string dirA;
    std::wstring dirW;
    dirA = URIUtils::GetDirectory(pFileItem.GetPath());
    g_charsetConverter.utf8ToW(dirA, dirW);

    hr = CGraphFilters::Get()->DVD.dvdControl->SetDVDDirectory(dirW.c_str());
    if (FAILED(hr))
      CLog::Log(LOGERROR, "{} Failed loading dvd directory.", __FUNCTION__);

    CGraphFilters::Get()->DVD.dvdControl->SetOption(DVD_ResetOnStop, FALSE);
    CGraphFilters::Get()->DVD.dvdControl->SetOption(DVD_HMSF_TimeCodeEvents, TRUE);

    return hr;
  }

  if (StringUtils::EqualsNoCase(filterName, "internal_urlsource"))
  {
    //TODO
    //add IAMOpenProgress for requesting the status of stream without this interface the player failed if connection is too slow
    Com::SComQIPtr<IFileSourceFilter> pSourceUrl;
    Com::SComPtr<IUnknown> pUnk = NULL;

    pUnk.CoCreateInstance(CLSID_URLReader, NULL);
    hr = pUnk->QueryInterface(IID_IBaseFilter, (void**)&CGraphFilters::Get()->Source.pBF);
    if (SUCCEEDED(hr))
    {
      hr = g_dsGraph->pFilterGraph->AddFilter(CGraphFilters::Get()->Source.pBF, L"URLReader");
      CGraphFilters::Get()->Source.osdname = "URLReader";
      std::wstring strUrlW; g_charsetConverter.utf8ToW(pFileItem.GetPath(), strUrlW);

      if (pSourceUrl = pUnk)
        hr = pSourceUrl->Load(strUrlW.c_str(), NULL);

      if (FAILED(hr))
      {
        g_dsGraph->pFilterGraph->RemoveFilter(CGraphFilters::Get()->Source.pBF);
        CLog::Log(LOGERROR, "{} Failed to add url source filter to the graph.", __FUNCTION__);
        CGraphFilters::Get()->Source.pBF = NULL;
        return E_FAIL;
      }

      ParseStreamingType(pFileItem, CGraphFilters::Get()->Source.pBF);

      return S_OK;
    }
  }

  /* Two cases:
  1/ The source filter is also a splitter. We insert it to the graph as a splitter and load the file
  2/ The source filter is only a source filter. Add it to the graph as a source filter
  */

  // TODO: Loading an url must be done!

  // If the source filter has more than one ouput pin, it's a splitter too.
  // Only problem, the file must be loaded in the source filter to see the
  // number of output pin
  CURL url(pFileItem.GetPath());

  std::string pWinFilePath = url.Get();
  /*
  * Convert SMB to windows UNC
  * SMB: smb://HOSTNAME/share/file.ts
  * SMB: smb://user:pass@HOSTNAME/share/file.ts
  * windows UNC: \\\\HOSTNAME\share\file.ts
  */
  pWinFilePath = CDSFile::SmbToUncPath(pWinFilePath);
  
  if (!URIUtils::IsInternetStream(pFileItem.GetDynPath(), true))
    StringUtils::Replace(pWinFilePath, "/", "\\");

  // Report what libbluray makes of the disc before the graph is built, so the result is
  // logged even when the source filter cannot load the file
  CDSBlurayProbe::Probe(pWinFilePath);

  // Our own disc sources read through Kodi rather than opening the file themselves, so they
  // want the path Kodi uses -- which for the DVD source is also what decides whether the
  // image is read through the virtual file system instead of being opened as a device.
  // Everything else is a DirectShow filter that can only open a Windows path.
  const bool wantsKodiPath =
      (filterName == INTERNAL_BLURAY_SOURCE) || (filterName == INTERNAL_DVD_SOURCE);

  std::wstring strFileW;
  g_charsetConverter.utf8ToW(wantsKodiPath ? pFileItem.GetDynPath() : pWinFilePath, strFileW,
                             false);
  SFilterInfos infos;
  try // Load() may crash on bad designed codec. Prevent XBMC to hang
  {
    if (FAILED(hr = InsertFilter(filterName, infos)))
    {
      if (infos.isinternal)
        delete infos.pData;
      return E_FAIL;
    }

    Com::SComQIPtr<IFileSourceFilter> pFS = infos.pBF.p;

    if (SUCCEEDED(pFS->Load(strFileW.c_str(), NULL)))
      CLog::Log(LOGINFO, "{} Successfully loaded file in the splitter/source", __FUNCTION__);
    else
    {
      CLog::Log(LOGERROR, "{} Failed to load file in the splitter/source", __FUNCTION__);

      if (infos.isinternal)
        delete infos.pData;

      return E_FAIL;
    }
  }
  catch (...) {
    CLog::Log(LOGERROR, "{} An exception has been thrown by the codec...", __FUNCTION__);

    if (infos.isinternal)
      delete infos.pData;

    return E_FAIL;
  }

  bool isSplitterToo = IsSplitter(infos.pBF);
  if ((pFileItem.GetVideoInfoTag()->m_streamDetails.GetVideoStreamCount() == 1 && pFileItem.GetVideoInfoTag()->m_streamDetails.GetAudioStreamCount() == 0) 
    || (GetCLSID(infos.pBF) == CLSID_LAVSplitterSource))
    isSplitterToo = true;

  if (isSplitterToo)
  {
    CLog::Log(LOGDEBUG, "{} The source filter is also a splitter.", __FUNCTION__);
    CGraphFilters::Get()->Splitter = infos;
  }
  else {
    CGraphFilters::Get()->Source = infos;
  }

  ParseStreamingType(pFileItem, infos.pBF);

  return hr;
}

void CFGLoader::ParseStreamingType(CFileItem& pFileItem, IBaseFilter* pBF)
{
  // Detect the type of the streaming, in order to choose the right splitter
  // Often, streaming url does not have an extension ...
  GUID guid;
  BeginEnumPins(pBF, pEP, pPin)
  {
    PIN_DIRECTION pPinDir;
    pPin->QueryDirection(&pPinDir);
    if (pPinDir != PINDIR_OUTPUT)
      continue;

    BeginEnumMediaTypes(pPin, pEMT, pMT)
    {
      if (pMT->majortype != MEDIATYPE_Stream)
        continue;

      std::string str;
      g_charsetConverter.wToUTF8(GetMediaTypeName(pMT->subtype), str);
      CLog::Log(LOGDEBUG, __FUNCTION__" Streaming output pin media tpye: {}", str.c_str());
      guid = pMT->subtype;
    }
    EndEnumMediaTypes(pMT);
  }
  EndEnumPins;

}

HRESULT CFGLoader::InsertSplitter(const CFileItem& pFileItem, const std::string& filterName)
{
  HRESULT hr = InsertFilter(filterName, CGraphFilters::Get()->Splitter);

  if (SUCCEEDED(hr))
  {
    if (SUCCEEDED(hr = ConnectFilters(g_dsGraph->pFilterGraph, CGraphFilters::Get()->Source.pBF, CGraphFilters::Get()->Splitter.pBF)))
      CLog::Log(LOGINFO, "{} Successfully connected the source to the splitter", __FUNCTION__);
    else
      CLog::Log(LOGERROR, "{} Failed to connect the source to the splitter", __FUNCTION__);
  }

  return hr;
}

HRESULT CFGLoader::InsertAudioRenderer(const std::string& filterName)
{
  HRESULT hr = S_FALSE;
  CFGFilterRegistry* pFGF;
  std::string sAudioRenderName, sAudioRenderDisplayName;
  if (!filterName.empty())
  {
    if (SUCCEEDED(InsertFilter(filterName, CGraphFilters::Get()->AudioRenderer)))
      return S_OK;
    else
      CLog::Log(LOGERROR, "{} Failed to insert custom audio renderer, fallback to default one", __FUNCTION__);
  }

  std::vector<DSFilterInfo> deviceList;
  START_PERFORMANCE_COUNTER
    CAudioEnumerator p_dsound;
  p_dsound.GetAudioRenderers(deviceList);
  END_PERFORMANCE_COUNTER("Loaded audio renderer list");

  //see if there a config first 
  std::string renderer = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_AUDIORENDERER);

  if (renderer == CGraphFilters::INTERNAL_SANEAR)
  {
    struct SaneAudioRendererFilter : CFGFilter {
      SaneAudioRendererFilter(std::string name) :
        CFGFilter(SaneAudioRenderer::Factory::GetFilterGuid(), CFGFilter::FILE, name) {}

      HRESULT Create(IBaseFilter** ppBF) override {
        return SaneAudioRenderer::Factory::CreateFilter(CGraphFilters::Get()->sanear, ppBF);
      }
    };

    CGraphFilters::Get()->SetSanearSettings();

    CFGFilter *pFilter;
    START_PERFORMANCE_COUNTER
      pFilter = DNew SaneAudioRendererFilter("(i) Sanear Audio Renderer");
    hr = pFilter->Create(&CGraphFilters::Get()->AudioRenderer.pBF);
    delete pFilter;
    END_PERFORMANCE_COUNTER("Loaded internal sanear audio renderer");

    if (SUCCEEDED(hr))
    {
      START_PERFORMANCE_COUNTER
        hr = g_dsGraph->pFilterGraph->AddFilter(CGraphFilters::Get()->AudioRenderer.pBF, L"(i) Sanear Audio Renderer");
      END_PERFORMANCE_COUNTER("Added internal sanear audio renderer to the graph");

      CGraphFilters::Get()->AudioRenderer.internalFilter = true;
      CLog::Log(LOGINFO, "{} Successfully added internal sanear audio renderer to the graph", __FUNCTION__);
    } 
    else
    {
      CLog::Log(LOGERROR, "{} Failed to create the intenral audio renderer (%X)", __FUNCTION__, hr);
    }
  }
  
  //TODO fix if config not there
  if (renderer == "System Default")
    renderer = std::string("Default DirectSound Device");
  if (renderer != CGraphFilters::INTERNAL_SANEAR || FAILED(hr))
  {
    //for (std::vector<DSFilterInfo>::const_iterator iter = deviceList.begin(); !renderer.empty() && (iter != deviceList.end()); ++iter)
    for (std::vector<DSFilterInfo>::const_iterator iter = deviceList.begin(); (iter != deviceList.end()); ++iter)
    {
      DSFilterInfo dev = *iter;
      if (StringUtils::EqualsNoCase(renderer, dev.lpstrName))//renderer
      {
        sAudioRenderName = dev.lpstrName;
        sAudioRenderDisplayName = dev.lpstrDisplayName;
        START_PERFORMANCE_COUNTER
          //pFGF = new CFGFilterRegistry(GUIDFromString(L"DF557071-C9FD-433A-9627-81E0D3640ED9"));
          pFGF = new CFGFilterRegistry(dev.lpstrDisplayName);
        hr = pFGF->Create(&CGraphFilters::Get()->AudioRenderer.pBF);
        delete pFGF;
        END_PERFORMANCE_COUNTER("Loaded audio renderer from registry");

        if (FAILED(hr))
        {
          CLog::Log(LOGERROR, "{} Failed to create the audio renderer (%X)", __FUNCTION__, hr);
          return hr;
        }

        START_PERFORMANCE_COUNTER
          std::wstring sNameW;
        g_charsetConverter.utf8ToW(dev.lpstrName, sNameW);
        hr = g_dsGraph->pFilterGraph->AddFilter(CGraphFilters::Get()->AudioRenderer.pBF, sNameW.c_str());
        END_PERFORMANCE_COUNTER("Added audio renderer to the graph");

        break;
      }
    }  
    
  if (SUCCEEDED(hr))
    CLog::Log(LOGINFO, "{} Successfully added \"{}\" - DiplayName: {} to the graph", __FUNCTION__, sAudioRenderName.c_str(), sAudioRenderDisplayName.c_str());
  else
    CLog::Log(LOGINFO, "{} Failed to add \"{}\" to the graph (result: %X)", __FUNCTION__, sAudioRenderName.c_str(), hr);
  }

  return hr;
}

HRESULT CFGLoader::InsertVideoRenderer()
{
  HRESULT hr = S_OK;

  CStdStringA videoRender;
  videoRender = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_VIDEORENDERER);
  if (videoRender.length() == 0)
    videoRender = "evr";

  if (videoRender.ToLower() == "madvr")
  {
    m_pFGF = new CFGFilterVideoRenderer(CLSID_madVRAllocatorPresenter, "Kodi madVR");
  }
  else if (videoRender.ToLower() == "mpcvr")
  {
    m_pFGF = new CFGFilterVideoRenderer(__uuidof(CMpcVideoRenderer), "Kodi mpcVR");
  }

  hr = m_pFGF->Create(&CGraphFilters::Get()->VideoRenderer.pBF);
  if (FAILED(hr))
  {
    CLog::Log(LOGERROR, "{} Failed to create allocator presenter (hr = %X)", __FUNCTION__, hr);
    return hr;
  }
  hr = g_dsGraph->pFilterGraph->AddFilter(CGraphFilters::Get()->VideoRenderer.pBF, m_pFGF->GetNameW().c_str());

  /* Query IQualProp from the renderer */
  CGraphFilters::Get()->VideoRenderer.pBF->QueryInterface(IID_IQualProp, (void **)&CGraphFilters::Get()->VideoRenderer.pQualProp);

  if (SUCCEEDED(hr))
  {
    CLog::Log(LOGDEBUG, "{} Allocator presenter successfully added to the graph (Renderer: {})", __FUNCTION__, CGraphFilters::Get()->VideoRenderer.osdname.c_str());
  }
  else {
    CLog::Log(LOGERROR, "{} Failed to add allocator presenter to the graph (hr = %X)", __FUNCTION__, hr);
  }

  return hr;
}

HRESULT CFGLoader::LoadFilterRules(const CFileItem& _pFileItem)
{
  CFileItem pFileItem = _pFileItem;

  if (!URIUtils::IsInternetStream(pFileItem.GetDynPath()) && !CFilterCoreFactory::SomethingMatch(pFileItem))
  {

    CLog::Log(LOGERROR, "{} Extension \"{}\" not found. Please check mediasconfig.xml",
      __FUNCTION__, CURL(pFileItem.GetPath()).GetFileType().c_str());
    CGUIDialogOK *dialog = (CGUIDialogOK *)CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_OK);
    if (dialog)
    {
      dialog->SetHeading("Extension not found");
      dialog->SetLine(0, "Impossible to play the media file : the media");
      dialog->SetLine(1, "extension \"" + CURL(pFileItem.GetPath()).GetFileType() + "\" isn't declared in mediasconfig.xml.");
      dialog->SetLine(2, "");
      CDSPlayer::errorWindow = dialog;
    }
    return E_FAIL;
  }

  std::string filter = "";

  START_PERFORMANCE_COUNTER
    CFilterCoreFactory::GetAudioRendererFilter(pFileItem, filter);
  InsertAudioRenderer(filter); // First added, last connected
  END_PERFORMANCE_COUNTER("Loading audio renderer");

  START_PERFORMANCE_COUNTER
    InsertVideoRenderer();
  END_PERFORMANCE_COUNTER("Loading video renderer");

  // We *need* those informations for filter loading. If the user wants it, be sure it's loaded
  // before using it.
  bool hasStreamDetails = false;
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_MYVIDEOS_EXTRACTFLAGS) &&
    pFileItem.HasVideoInfoTag() && !pFileItem.GetVideoInfoTag()->HasStreamDetails())
  {
    CLog::Log(LOGDEBUG, "{} - trying to extract filestream details from video file {}", __FUNCTION__, CURL::GetRedacted(pFileItem.GetPath()).c_str());
    hasStreamDetails = CDVDFileInfo::GetFileStreamDetails(&pFileItem);
  }
  else
    hasStreamDetails = pFileItem.HasVideoInfoTag() && pFileItem.GetVideoInfoTag()->HasStreamDetails();

  START_PERFORMANCE_COUNTER
    if (FAILED(CFilterCoreFactory::GetSourceFilter(pFileItem, filter)))
    {
    CLog::Log(LOGERROR, __FUNCTION__" Failed to get the source filter");
    return E_FAIL;
    }

  if (FAILED(InsertSourceFilter(pFileItem, filter)))
  {
    CLog::Log(LOGERROR, __FUNCTION__" Failed to insert the source filter");
    return E_FAIL;
  }
  END_PERFORMANCE_COUNTER("Loading source filter");
  START_PERFORMANCE_COUNTER
    if (!CGraphFilters::Get()->Splitter.pBF)
    {
    if (FAILED(CFilterCoreFactory::GetSplitterFilter(pFileItem, filter)))
      return E_FAIL;

    if (FAILED(InsertSplitter(pFileItem, filter)))
    {
      return E_FAIL;
    }
    }
  END_PERFORMANCE_COUNTER("Loading splitter filter");

  //subtitles not right noew

  START_PERFORMANCE_COUNTER

    if (SUCCEEDED(CFilterCoreFactory::GetSubsFilter(pFileItem, filter, CGraphFilters::Get()->IsUsingDXVADecoder())))
    {
      // XyVSFilter draws subtitles into the picture, so it sits in the video path and hands
      // the renderer a media type of its own making - one without the colour information the
      // decoder put on the original. madVR then has nothing to go on and guesses BT.709, so
      // an HDR film is played as if it were SDR and the display never switches to HDR (it
      // also costs DXVA decoding). XySubFilter draws through madVR's own subtitle interface
      // instead and leaves the video untouched, which is exactly why it exists. So only fall
      // back to XyVSFilter for renderers that cannot take subtitles that way.
      const bool isMadvr =
          StringUtils::EqualsNoCase(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(
                                        CSettings::SETTING_DSPLAYER_VIDEORENDERER),
                                    "madvr");

      if (!isMadvr
        && CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_FILTERSMANAGEMENT) == INTERNALFILTERS
        && filter == CGraphFilters::INTERNAL_XYSUBFILTER)
        filter = CGraphFilters::INTERNAL_XYVSFILTER;

      // A subtitle filter is optional. Failing to add it must not abort graph building,
      // otherwise a media rule naming a filter that isn't installed on the system makes
      // the file completely unplayable instead of just losing subtitle support.
      if (FAILED(InsertFilter(filter, CGraphFilters::Get()->Subs)))
        CLog::Log(LOGWARNING, "{} Unable to add the subtitle filter \"{}\" to the graph. "
                              "Playback continues without subtitle support", __FUNCTION__, filter.c_str());
      END_PERFORMANCE_COUNTER("Loading subs filter");
    }

    // Init Streams manager, and load streams
    START_PERFORMANCE_COUNTER
      CStreamsManager::Create();
    CStreamsManager::Get()->InitManager();
    CStreamsManager::Get()->LoadStreams(pFileItem.GetPath());
    END_PERFORMANCE_COUNTER("Loading streams informations");

    if (!hasStreamDetails) {
      // We will use our own stream detail
      // We need to make a copy of our streams details because
      // Reset() delete the streams
      if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_MYVIDEOS_EXTRACTFLAGS)) // Only warn user if the option is enabled
        CLog::Log(LOGWARNING, __FUNCTION__" VideoPlayer failed to fetch streams details. Using DirectShow ones");

      CLog::Log(LOGDEBUG, "{} copying stream details, video detail {}", __FUNCTION__,
                CStreamsManager::Get()->GetVideoStreamDetail() ? "present" : "MISSING");

      pFileItem.GetVideoInfoTag()->m_streamDetails.AddStream(
        new CDSStreamDetailVideo((const CDSStreamDetailVideo &)(*CStreamsManager::Get()->GetVideoStreamDetail()))
        );
      std::vector<CDSStreamDetailAudio *>& streams = CStreamsManager::Get()->GetAudios();
      CLog::Log(LOGDEBUG, "{} copying {} audio detail(s)", __FUNCTION__, streams.size());

      for (std::vector<CDSStreamDetailAudio *>::const_iterator it = streams.begin();
        it != streams.end(); ++it)
        pFileItem.GetVideoInfoTag()->m_streamDetails.AddStream(
        new CDSStreamDetailAudio((const CDSStreamDetailAudio &)(**it))
        );

      CLog::Log(LOGDEBUG, "{} stream details copied", __FUNCTION__);
    }

    std::vector<std::string> extras;
    START_PERFORMANCE_COUNTER
      if (FAILED(CFilterCoreFactory::GetExtraFilters(pFileItem, extras, CGraphFilters::Get()->IsUsingDXVADecoder())))
      {
      //Dont want the loading to fail for an error there
      CLog::Log(LOGERROR, "Failed loading extras filters in filtersconfig.xml");
      }

    // Insert extra first because first added, last connected!
    for (unsigned int i = 0; i < extras.size(); i++)
    {
      SFilterInfos f;
      if (SUCCEEDED(InsertFilter(extras[i], f)))
        CGraphFilters::Get()->Extras.push_back(f);
    }
    CLog::Log(LOGDEBUG, "{} extras inserted, reading the filter management setting", __FUNCTION__);
    if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_FILTERSMANAGEMENT) == INTERNALFILTERS)
    {
      CLog::Log(LOGDEBUG, "{} internal filters, checking for extra filter settings", __FUNCTION__);
      for (unsigned int i = 0; i < 3; i++)
      {
        CLog::Log(LOGDEBUG, "{} extra filter slot {}", __FUNCTION__, i);

        std::string filter;
        // "%i" is printf syntax and StringUtils::Format takes fmt syntax, so this asked for
        // a setting literally named dsplayer.extrafilter%i, which does not exist. The
        // lookup failed every time and the three configured slots were never read.
        std::string setting = "dsplayer.extrafilter" + std::to_string(i);
        CLog::Log(LOGDEBUG, "{} extra filter slot {}, reading \"{}\"", __FUNCTION__, i, setting);
        filter = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(setting);
        CLog::Log(LOGDEBUG, "{} extra filter slot {} is \"{}\"", __FUNCTION__, i, filter);
        if (filter != "[null]")
        {
          SFilterInfos f;
          if (SUCCEEDED(InsertFilter(filter, f)))
            CGraphFilters::Get()->Extras.push_back(f);
          CLog::Log(LOGDEBUG, "{} extra filter slot {} handled", __FUNCTION__, i);
        }
        else
          break;
      }
    }
    extras.clear();
    END_PERFORMANCE_COUNTER("Loading extra filters");

    START_PERFORMANCE_COUNTER
      if (FAILED(CFilterCoreFactory::GetVideoFilter(pFileItem, filter, CGraphFilters::Get()->IsUsingDXVADecoder())))
        goto clean;

    if (FAILED(InsertFilter(filter, CGraphFilters::Get()->Video)))
      goto clean;

    END_PERFORMANCE_COUNTER("Loading video filter");

    START_PERFORMANCE_COUNTER
      if (FAILED(CFilterCoreFactory::GetAudioFilter(pFileItem, filter, CGraphFilters::Get()->IsUsingDXVADecoder())))
        goto clean;

    if (FAILED(InsertFilter(filter, CGraphFilters::Get()->Audio)))
      goto clean;

    CStreamsManager::Get()->SetAudioInterface();
    END_PERFORMANCE_COUNTER("Loading audio filter");


    // Shaders
    {
      std::vector<uint32_t> shaders;
      std::vector<uint32_t> shadersStages;
      START_PERFORMANCE_COUNTER
        if (SUCCEEDED(CFilterCoreFactory::GetShaders(pFileItem, shaders, shadersStages, CGraphFilters::Get()->IsUsingDXVADecoder())))
        {
        for (int unsigned i = 0; i < shaders.size(); i++)
        {
          g_dsSettings.pixelShaderList->EnableShader(shaders[i], shadersStages[i]);
        }
        }
      END_PERFORMANCE_COUNTER("Loading shaders");
    }

    CLog::Log(LOGDEBUG, "{} All filters added to the graph", __FUNCTION__);

    // If we have add our own informations, clear it to prevent xbmc to save it to the database
    if (!hasStreamDetails) {
      pFileItem.GetVideoInfoTag()->m_streamDetails.Reset();
    }
    return S_OK;

  clean:
    if (!hasStreamDetails) {
      pFileItem.GetVideoInfoTag()->m_streamDetails.Reset();
    }
    return E_FAIL;
}

HRESULT CFGLoader::LoadConfig(FILTERSMAN_TYPE filterManager)
{
  CXBMCTinyXML configXML;
  if (configXML.LoadFile("special://xbmc/system/players/dsplayer/dsplayerconfig.xml"))
  {
    TiXmlElement *pElement = configXML.RootElement();
    if (pElement && strcmpi(pElement->Value(), "settings") == 0)
    {
      XMLUtils::GetBoolean(pElement, "autorender", m_bIsAutoRender);
    }
  }
  // Two steps
  if (filterManager == NOFILTERMAN)
    filterManager = (FILTERSMAN_TYPE)CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_FILTERSMANAGEMENT);
  const std::shared_ptr<CProfileManager> profileManager = CServiceBroker::GetSettingsComponent()->GetProfileManager();
  if (filterManager == MEDIARULES)
  {
    // First, filters
    LoadFilterCoreFactorySettings(profileManager->GetUserDataItem("dsplayer/filtersconfig.xml"), FILTERS, true);
    LoadFilterCoreFactorySettings("special://xbmc/system/players/dsplayer/filtersconfig.xml", FILTERS, false);

    // Second, medias rules
    LoadFilterCoreFactorySettings(CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/mediasconfig.xml"), MEDIAS, false);
    LoadFilterCoreFactorySettings("special://xbmc/system/players/dsplayer/mediasconfig.xml", MEDIAS, false, 100);
  }
  else if (filterManager == INTERNALFILTERS)
  {
    LoadFilterCoreFactorySettings(CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/filtersconfig.xml"), FILTERS, false);
    LoadFilterCoreFactorySettings(CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/mediasconfig.xml"), MEDIAS, false);
    LoadFilterCoreFactorySettings("special://xbmc/system/players/dsplayer/filtersconfig.xml", FILTERS, false);
    LoadFilterCoreFactorySettings("special://xbmc/system/players/dsplayer/mediasconfig.xml", MEDIAS, false);
  }

  CFilterCoreFactory::DebugRules();

  return S_OK;
}

HRESULT CFGLoader::InsertFilter(const std::string& filterName, SFilterInfos& f)
{
  HRESULT hr = S_OK;
  f.pBF = NULL;

  CFGFilter *filter = NULL;
  if (!(filter = CFilterCoreFactory::GetFilterFromName(filterName)))
    return E_FAIL;

  if (SUCCEEDED(hr = filter->Create(&f.pBF)))
  {
    // Setup InternalFilters settings
    if (filterName == "lavsplitter_internal" || filterName == "lavsource_internal")
    {
      f.internalFilter = true;
      CGraphFilters::Get()->SetupLavSettings(CGraphFilters::INTERNAL_LAVSPLITTER, f.pBF);
    }
    if (filterName == CGraphFilters::INTERNAL_LAVVIDEO)
    {
      f.internalFilter = true;
      CGraphFilters::Get()->SetupLavSettings(CGraphFilters::INTERNAL_LAVVIDEO, f.pBF);
    }
    if (filterName == CGraphFilters::INTERNAL_LAVAUDIO)
    {
      f.internalFilter = true;
      CGraphFilters::Get()->SetupLavSettings(CGraphFilters::INTERNAL_LAVAUDIO, f.pBF);
    }
    if (filterName == CGraphFilters::INTERNAL_XYSUBFILTER || filterName == CGraphFilters::INTERNAL_XYVSFILTER)
      f.internalFilter = true;

    f.osdname = filter->GetName();
    if (filter->GetType() == CFGFilter::INTERNAL)
    {
      CLog::Log(LOGDEBUG, "{} Using an internal filter", __FUNCTION__);
      f.isinternal = true;
      f.pData = filter;
    }
    if (SUCCEEDED(hr = g_dsGraph->pFilterGraph->AddFilter(f.pBF, filter->GetNameW().c_str())))
      CLog::Log(LOGINFO, "{} Successfully added \"{}\" to the graph", __FUNCTION__, f.osdname.c_str());
    else
      CLog::Log(LOGERROR, "{} Failed to add \"{}\" to the graph", __FUNCTION__, f.osdname.c_str());

    f.guid = filter->GetCLSID();
  }
  else
  {
    CLog::Log(LOGERROR, "{} Failed to create filter \"{}\"", __FUNCTION__, filterName.c_str());
  }

  return hr;
}

void CFGLoader::SettingOptionsDSVideoRendererFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current, void* data)
{
  CLog::Log(LOGINFO, "{}", __FUNCTION__);

  //Internal renderer always added
  list.push_back(StringSettingOption("MPC Video Renderer highly modified", "mpcvr"));
  
  
  CDSFilterEnumerator p_dsfilter;
  std::vector<DSFiltersInfo> dsfilterList;
  p_dsfilter.GetDSFilters(dsfilterList);
  std::vector<DSFiltersInfo>::const_iterator iter = dsfilterList.begin();
  //add madvr if filter registered
  for (int i = 1; iter != dsfilterList.end(); i++)
  {
    DSFiltersInfo dev = *iter;
    if (dev.lpstrName == "madVR")
    {
      list.push_back(StringSettingOption("madshi Video Renderer (madVR)", "madvr"));
      break;
    }
    ++iter;
  }

}

void CFGLoader::SettingOptionsDSAudioRendererFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current, void* data)
{

  list.push_back(StringSettingOption("Internal Audio Renderer (Sanear)", CGraphFilters::INTERNAL_SANEAR));
  list.push_back(StringSettingOption("System Default", "System Default"));

  std::vector<DSFilterInfo> deviceList;
  CAudioEnumerator p_dsound;
  p_dsound.GetAudioRenderers(deviceList);
  std::vector<DSFilterInfo>::const_iterator iter = deviceList.begin();

  for (int i = 1; iter != deviceList.end(); i++)
  {
    DSFilterInfo dev = *iter;
    list.push_back(StringSettingOption(dev.lpstrName, dev.lpstrName));
    ++iter;
  }

}

void CFGLoader::SettingOptionsSanearDevicesFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current, void* data)
{
  //ADD every device here
  
  list.push_back(StringSettingOption("System Default", "System Default"));
  for (const auto& device : GetDevices())
  {
    list.push_back(StringSettingOption(device.first, device.second));// std::make_pair(device.first, device.second));
  }

}

bool CFGLoader::LoadFilterCoreFactorySettings(const std::string& fileStr, ESettingsType type, bool clear, int iPriority)
{
  if (clear)
  {
    CFilterCoreFactory::Destroy();
  }

  CLog::Log(LOGINFO, "Loading filter core factory settings from {} ({} configuration).", CSpecialProtocol::TranslatePath(fileStr).c_str(), (type == MEDIAS) ? "medias" : "filters");
  
  if (!XFILE::CFile::Exists(fileStr))
  { // tell the user it doesn't exist
    
    CLog::Log(LOGINFO, "{} does not exist. Skipping.", fileStr.c_str());
    return false;
  }

  CXBMCTinyXML filterCoreFactoryXML;
  if (!filterCoreFactoryXML.LoadFile(fileStr))
  {
    CLog::Log(LOGERROR, "Error loading {}, Line %d ({})", fileStr.c_str(), filterCoreFactoryXML.ErrorRow(), filterCoreFactoryXML.ErrorDesc());
    return false;
  }

  return ((type == MEDIAS) ? SUCCEEDED(CFilterCoreFactory::LoadMediasConfiguration(filterCoreFactoryXML.RootElement(), iPriority))
    : SUCCEEDED(CFilterCoreFactory::LoadFiltersConfiguration(filterCoreFactoryXML.RootElement())));
}

#endif
