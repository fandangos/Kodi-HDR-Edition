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

#include "GUIDialogLAVVideo.h"
#include "Application/Application.h"
#include "URL.h"
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/File.h"
#include "resources/LocalizeStrings.h"
#include "profiles/ProfileManager.h"
#include "settings/Settings.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "utils/LangCodeExpander.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "input/keyboard/Key.h"
#include "utils/XMLUtils.h"
#include "Filters/RendererSettings.h"
#include "PixelShaderList.h"
#include "cores/playercorefactory/PlayerCoreFactory.h"
#include "Filters/LAVAudioSettings.h"
#include "Filters/LAVVideoSettings.h"
#include "Filters/LAVSplitterSettings.h"
#include "utils/CharsetConverter.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "addons/Skin.h"
#include "GraphFilters.h"
#include "settings/SettingUtils.h"
#include "ServiceBroker.h"
#include "guilib/GUIComponent.h"

#define LAVVIDEO_PROPERTYPAGE      "lavvideo.propertypage"
#define LAVVIDEO_HWACCEL           "lavvideo.hwaccel"
#define LAVVIDEO_HWACCELDEVICES    "lavvideo.hwacceldevices"
#define LAVVIDEO_HWACCELRES        "lavvideo.hwaccelres"
#define LAVVIDEO_HWACCELCODECS     "lavvideo.hwaccelcodecs"
#define LAVVIDEO_NUMTHREADS        "lavvideo.dwnumthreads"
#define LAVVIDEO_TRAYICON          "lavvideo.btrayicon"
#define LAVVIDEO_STREAMAR          "lavvideo.dwstreamar"
#define LAVVIDEO_DEINTFILEDORDER   "lavvideo.dwdeintfieldorder"
#define LAVVIDEO_DEINTMODE         "lavvideo.deintmode"
#define LAVVIDEO_RGBRANGE          "lavvideo.dwrgbrange"
#define LAVVIDEO_DITHERMODE        "lavvideo.dwdithermode"
#define LAVVIDEO_HWDEINTMODE       "lavvideo.dwhwdeintmode"
#define LAVVIDEO_HWDEINTOUT        "lavvideo.dwhwdeintoutput"
#define LAVVIDEO_SWDEINTMODE       "lavvideo.dwswdeintmode"
#define LAVVIDEO_SWDEINTOUT        "lavvideo.dwswdeintoutput"
#define LAVVIDEO_RESET             "lavvideo.reset"

using namespace std;

CGUIDialogLAVVideo::CGUIDialogLAVVideo()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_LAVVIDEO, "DialogSettings.xml")
{
}

CGUIDialogLAVVideo::~CGUIDialogLAVVideo()
{ 
}

void CGUIDialogLAVVideo::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(55077);

  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogLAVVideo::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  bool usePopup = CServiceBroker::GetGUI()->GetSkinInfo()->HasSkinFile("DialogSlider.xml");

  std::shared_ptr<CSettingCategory> category = AddCategory("dsplayerlavvideo", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVVideo: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupProperty = AddGroup(category);
  if (groupProperty == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVVideo: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> group = AddGroup(category);
  if (group == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVVideo: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupHW = AddGroup(category);
  if (groupHW == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVVideo: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupSettings = AddGroup(category);
  if (groupSettings == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVVideo: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupOutput = AddGroup(category);
  if (groupOutput == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVVideo: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupDeintSW = AddGroup(category);
  if (groupDeintSW == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVVideo: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupDeintHW = AddGroup(category);
  if (groupDeintHW == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVVideo: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupReset = AddGroup(category);
  if (groupReset == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
#if TODO
  // Get settings from the current running filter
  IBaseFilter *pBF;
  CGraphFilters::Get()->GetInternalFilter(CGraphFilters::INTERNAL_LAVVIDEO, &pBF);
  CGraphFilters::Get()->GetLavSettings(CGraphFilters::INTERNAL_LAVVIDEO, pBF);

  std::vector<IntegerSettingOption> entries;
  CLavSettings &lavSettings = CMediaSettings::GetInstance().GetCurrentLavSettings();

  // BUTTON
  AddButton(groupProperty, LAVVIDEO_PROPERTYPAGE, 80013, SettingLevel::Basic);

  // TRAYICON
  AddToggle(group, LAVVIDEO_TRAYICON, 80001, SettingLevel::Basic, lavSettings.video_bTrayIcon);

  // HW ACCELERATION
  
  // dependencies
  CSettingDependency dependencyHWAccelEnabled(SettingDependencyType::Enable, GetSettingsManager());
  dependencyHWAccelEnabled.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(LAVVIDEO_HWACCEL, "0", SettingDependencyOperator::Equals, true, GetSettingsManager())));
  SettingDependencies depsHWAccelEnabled;
  depsHWAccelEnabled.push_back(dependencyHWAccelEnabled);

  entries.clear();
  entries.emplace_back(80200, HWAccel_None);
  entries.emplace_back(80201, HWAccel_CUDA);
  entries.emplace_back(80202, HWAccel_QuickSync);
  entries.emplace_back(80203, HWAccel_DXVA2CopyBack);
  entries.emplace_back(80204, HWAccel_DXVA2Native);
  entries.emplace_back(80221, HWAccel_D3D11);
  AddList(groupHW, LAVVIDEO_HWACCEL, 80005, SettingLevel::Basic, lavSettings.video_dwHWAccel, entries, 80005);

  std::shared_ptr<CSetting>  settingHWDevices;
  settingHWDevices = AddList(groupHW, LAVVIDEO_HWACCELDEVICES, 80014, SettingLevel::Basic,
    lavSettings.video_dwHWAccelDeviceIndex[lavSettings.video_dwHWAccel], 
    HWAccellIndexFiller,
    80014);
  settingHWDevices->SetParent(LAVVIDEO_HWACCEL);
  settingHWDevices->SetDependencies(depsHWAccelEnabled);

  // HW RESOLUTIONS
  std::vector<int> values;
  if (lavSettings.video_dwHWAccelResFlags & LAVHWResFlag_SD)
    values.emplace_back(LAVHWResFlag_SD);
  if (lavSettings.video_dwHWAccelResFlags & LAVHWResFlag_HD)
    values.emplace_back(LAVHWResFlag_HD);
  if (lavSettings.video_dwHWAccelResFlags & LAVHWResFlag_UHD)
    values.emplace_back(LAVHWResFlag_UHD);
  std::shared_ptr<CSettingInt>  *settingHWRes;

  settingHWRes = AddList(groupHW, LAVVIDEO_HWACCELRES, 80015,SettingLevel::Basic, values, ResolutionsFiller, 80015);
  settingHWRes->SetParent(LAVVIDEO_HWACCEL);
  settingHWRes->SetDependencies(depsHWAccelEnabled);

  // HW CODECS
  values.clear();
  if (lavSettings.video_bHWFormats[HWCodec_H264])
    values.emplace_back(HWCodec_H264);
  if (lavSettings.video_bHWFormats[HWCodec_VC1])
    values.emplace_back(HWCodec_VC1);
  if (lavSettings.video_bHWFormats[HWCodec_MPEG2])
    values.emplace_back(HWCodec_MPEG2);
  if (lavSettings.video_bHWFormats[HWCodec_MPEG2DVD])
    values.emplace_back(HWCodec_MPEG2DVD);
  if (lavSettings.video_bHWFormats[HWCodec_HEVC])
    values.emplace_back(HWCodec_HEVC);
  if (lavSettings.video_bHWFormats[HWCodec_VP9])
    values.emplace_back(HWCodec_VP9);
  std::shared_ptr<CSetting>  *settingHWCodecs;
  settingHWCodecs = AddList(groupHW, LAVVIDEO_HWACCELCODECS, 80016, SettingLevel::Basic, values, CodecsFiller, 80016);
  settingHWCodecs->SetParent(LAVVIDEO_HWACCEL);
  settingHWCodecs->SetDependencies(depsHWAccelEnabled);

  entries.clear();
  for (unsigned int i = 0; i < 17; i++)
    entries.emplace_back(80100 + i, i);
  AddList(groupHW, LAVVIDEO_NUMTHREADS, 80003, SettingLevel::Basic, lavSettings.video_dwNumThreads, entries, 80003);

  // SETTINGS
  AddToggle(groupSettings, LAVVIDEO_STREAMAR, 80002, SettingLevel::Basic, lavSettings.video_dwStreamAR);

  entries.clear();
  entries.emplace_back(80100, DeintFieldOrder_Auto);
  entries.emplace_back(80205, DeintFieldOrder_TopFieldFirst);
  entries.emplace_back(80206, DeintFieldOrder_BottomFieldFirst);
  AddList(groupSettings, LAVVIDEO_DEINTFILEDORDER, 80009, SettingLevel::Basic, lavSettings.video_dwDeintFieldOrder, entries, 80009);

  entries.clear();
  entries.emplace_back(80100, DeintMode_Auto);
  entries.emplace_back(80207, DeintMode_Aggressive);
  entries.emplace_back(80208, DeintMode_Force);
  entries.emplace_back(80209, DeintMode_Disable);
  AddList(groupSettings, LAVVIDEO_DEINTMODE, 80010, SettingLevel::Basic, (LAVDeintMode)lavSettings.video_deintMode, entries, 80010);

  // OUTPUT RANGE
  entries.clear();
  entries.emplace_back(80214, 1); // "TV (16/235)"
  entries.emplace_back(80215, 2); // "PC (0-255)"
  entries.emplace_back(80216, 0); // "Untouched (as input)"
  AddList(groupOutput, LAVVIDEO_RGBRANGE, 80004, SettingLevel::Basic, lavSettings.video_dwRGBRange, entries, 80004);

  entries.clear();
  entries.emplace_back(80212, LAVDither_Ordered);
  entries.emplace_back(80213, LAVDither_Random);
  AddList(groupOutput, LAVVIDEO_DITHERMODE, 80012, SettingLevel::Basic, lavSettings.video_dwDitherMode, entries, 80012);

  // DEINT HW/SW

  // dependencies
  std::shared_ptr<CSetting> Dependency dependencyHWDeintEnabled(SettingDependencyTypeEnable, GetSettingsManager());
  dependencyHWDeintEnabled.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(LAVVIDEO_HWDEINTMODE, "true", SettingDependencyOperatorEquals, false, GetSettingsManager())));
  SettingDependencies depsHWDeintEnabled;
  depsHWDeintEnabled.push_back(dependencyHWDeintEnabled);

  AddToggle(groupDeintHW, LAVVIDEO_HWDEINTMODE, 80006, 0, lavSettings.video_dwHWDeintMode);
  entries.clear();
  entries.emplace_back(80210, DeintOutput_FramePer2Field); // "25p/30p (Film)"
  entries.emplace_back(80211, DeintOutput_FramePerField); // "50p/60p (Video)"  
  std::shared_ptr<CSetting>  *settingHWDeintOut;
  settingHWDeintOut = AddList(groupDeintHW, LAVVIDEO_HWDEINTOUT, 80007, 0, lavSettings.video_dwHWDeintOutput, entries, 80007);
  settingHWDeintOut->SetParent(LAVVIDEO_HWDEINTMODE);
  settingHWDeintOut->SetDependencies(depsHWDeintEnabled);

  // dependencies
  std::shared_ptr<CSetting> Dependency dependencySWDeintEnabled(SettingDependencyTypeEnable, GetSettingsManager());
  dependencySWDeintEnabled.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(LAVVIDEO_SWDEINTMODE, "0", SettingDependencyOperatorEquals, true, GetSettingsManager())));
  SettingDependencies depSWDeintEnabled;
  depSWDeintEnabled.push_back(dependencySWDeintEnabled);

  entries.clear();
  entries.emplace_back(80220, SWDeintMode_None);
  entries.emplace_back(80217, SWDeintMode_YADIF);
  entries.emplace_back(80218, SWDeintMode_W3FDIF_Simple);
  entries.emplace_back(80219, SWDeintMode_W3FDIF_Complex);
  AddList(groupDeintSW, LAVVIDEO_SWDEINTMODE, 80011, 0, lavSettings.video_dwSWDeintMode, entries, 800011);
  entries.clear();
  entries.emplace_back(80210, DeintOutput_FramePer2Field); // "25p/30p (Film)"
  entries.emplace_back(80211, DeintOutput_FramePerField); // "50p/60p (Video)"
  std::shared_ptr<CSetting>  *settingSWDeintOut;
  settingSWDeintOut = AddList(groupDeintSW, LAVVIDEO_SWDEINTOUT, 80007, 0, lavSettings.video_dwSWDeintOutput, entries, 80007);
  settingSWDeintOut->SetParent(LAVVIDEO_SWDEINTMODE);
  settingSWDeintOut->SetDependencies(depSWDeintEnabled);

  // BUTTON RESET
  if (!g_application.GetComponent<CApplicationPlayer>()->IsPlayingVideo())
    AddButton(groupReset, LAVVIDEO_RESET, 10041, 0);
#endif
}

void CGUIDialogLAVVideo::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CLavSettings &lavSettings = CMediaSettings::GetInstance().GetCurrentLavSettings();

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);
  const std::string &settingId = setting->GetId();

  if (settingId == LAVVIDEO_HWACCEL)
  {
    lavSettings.video_dwHWAccel = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
    std::shared_ptr<CSetting> setting_index = GetSettingsManager()->GetSetting(LAVVIDEO_HWACCELDEVICES);
    std::static_pointer_cast<CSettingInt>(setting_index)->UpdateDynamicOptions();
    std::static_pointer_cast<CSettingInt>(setting_index)->SetValue(lavSettings.video_dwHWAccelDeviceIndex[lavSettings.video_dwHWAccel]);
  }
  if (settingId == LAVVIDEO_HWACCELDEVICES)
  {
    lavSettings.video_dwHWAccelDeviceIndex[lavSettings.video_dwHWAccel] = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  }
  if (settingId == LAVVIDEO_HWACCELRES)
  {
#if TODO
    DWORD flags;
    std::vector<CVariant> values;
    values = CSettingUtils::GetList(static_cast<const CSettingList*>(setting));
    if (!values.empty())
    { 
      for (std::vector<CVariant>::const_iterator itValue = values.begin(); itValue != values.end(); ++itValue)
        flags |= itValue->asInteger();    
    }
    lavSettings.video_dwHWAccelResFlags = flags;

  }
  if (settingId == LAVVIDEO_HWACCELCODECS)
  {
    std::vector<CVariant> values;
    values = CSettingUtils::GetList(static_cast<const CSettingList*>(setting));

    lavSettings.video_bHWFormats[HWCodec_H264] = FALSE;
    lavSettings.video_bHWFormats[HWCodec_VC1] = FALSE;
    lavSettings.video_bHWFormats[HWCodec_MPEG2] = FALSE;
    lavSettings.video_bHWFormats[HWCodec_MPEG2DVD] = FALSE;
    lavSettings.video_bHWFormats[HWCodec_HEVC] = FALSE;
    lavSettings.video_bHWFormats[HWCodec_VP9] = FALSE;

    if (!values.empty())
    {
      for (std::vector<CVariant>::const_iterator itValue = values.begin(); itValue != values.end(); ++itValue)
        lavSettings.video_bHWFormats[itValue->asInteger()] = TRUE;
    }
#endif
  }

  if (settingId == LAVVIDEO_NUMTHREADS)
    lavSettings.video_dwNumThreads = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVVIDEO_TRAYICON)
    lavSettings.video_bTrayIcon = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVVIDEO_STREAMAR)
    lavSettings.video_dwStreamAR = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVVIDEO_DEINTFILEDORDER)
    lavSettings.video_dwDeintFieldOrder = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVVIDEO_DEINTMODE)
    lavSettings.video_deintMode = (LAVDeintMode)std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVVIDEO_RGBRANGE)
    lavSettings.video_dwRGBRange = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVVIDEO_DITHERMODE)
    lavSettings.video_dwDitherMode = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVVIDEO_HWDEINTMODE)
    lavSettings.video_dwHWDeintMode = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVVIDEO_HWDEINTOUT)
    lavSettings.video_dwHWDeintOutput = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVVIDEO_SWDEINTMODE)
    lavSettings.video_dwSWDeintMode = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVVIDEO_SWDEINTOUT)
    lavSettings.video_dwSWDeintOutput = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();

  // Get current running filter
  IBaseFilter *pBF;
  CGraphFilters::Get()->GetInternalFilter(CGraphFilters::INTERNAL_LAVVIDEO, &pBF);

  // Set settings changes into the running filter
  CGraphFilters::Get()->SetLavSettings(CGraphFilters::INTERNAL_LAVVIDEO, pBF);

  // Save new settings into DSPlayer DB
  CGraphFilters::Get()->SaveLavSettings(CGraphFilters::INTERNAL_LAVVIDEO);
}

void CGUIDialogLAVVideo::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingAction(setting);
  const std::string &settingId = setting->GetId();

  if (settingId == LAVVIDEO_PROPERTYPAGE)
  {
    CGraphFilters::Get()->ShowInternalPPage(CGraphFilters::INTERNAL_LAVVIDEO, true);
    this->Close();
  }

  if (settingId == LAVVIDEO_RESET)
  {
    if (!CGUIDialogYesNo::ShowAndGetInput(10041, 10042, 0, 0))
      return;

    CGraphFilters::Get()->EraseLavSetting(CGraphFilters::INTERNAL_LAVVIDEO);
    this->Close();
  }
}

void CGUIDialogLAVVideo::HWAccellIndexFiller(const std::shared_ptr<const CSetting>& setting, StringSettingOptions& list, std::string& current, void* data)
{
  CLavSettings &lavSettings = CMediaSettings::GetInstance().GetCurrentLavSettings();
  TranslatableStringSettingOptions itt;
  CGraphFilters::Get()->GetHWDeviceList(lavSettings.video_dwHWAccel, itt);
  for (TranslatableStringSettingOptions::iterator it = itt.begin(); it != itt.end(); it++)
  {

  }
  
}

void CGUIDialogLAVVideo::CodecsFiller(const std::shared_ptr<const CSetting>& setting, IntegerSettingOptions& list,  int& current,  void* data)
{
  list.emplace_back("h264", HWCodec_H264);
  list.emplace_back("VC1", HWCodec_VC1);
  list.emplace_back("MPEG2", HWCodec_MPEG2);
  list.emplace_back("DVD", HWCodec_MPEG2DVD);
  list.emplace_back("HEVC", HWCodec_HEVC);
  list.emplace_back("VP9", HWCodec_VP9);
}

void CGUIDialogLAVVideo::ResolutionsFiller(const std::shared_ptr<const CSetting>& setting, IntegerSettingOptions& list, int& current,  void* data)
{
  list.emplace_back("SD", LAVHWResFlag_SD);
  list.emplace_back("HD", LAVHWResFlag_HD);
  list.emplace_back("UHD (4k)", LAVHWResFlag_UHD);
}