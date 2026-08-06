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

#include "GUIDialogLAVAudio.h"
#include "Application/Application.h"
#include "URL.h"
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/File.h"
#include "resources/LocalizeStrings.h"
#include "profiles/ProfileManager.h"
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
#include "libavutil/channel_layout.h"
#include "application/ApplicationPlayer.h"
#include "ServiceBroker.h"
#include "guilib/GUIComponent.h"

#define LAVAUDIO_PROPERTYPAGE      "lavaudio.propertypage"
#define LAVAUDIO_TRAYICON          "lavaudio.trayicon"
#define LAVAUDIO_DRCENABLED        "lavaudio.drcenabled"
#define LAVAUDIO_DRCLEVEL          "lavaudio.drclevel"
#define LAVAUDIO_BITSTREAM_AC3     "lavaudio.bitstreamac3"
#define LAVAUDIO_BITSTREAM_EAC3    "lavaudio.bitstreameac3"
#define LAVAUDIO_BITSTREAM_TRUEHD  "lavaudio.bitstreamtruehd"
#define LAVAUDIO_BITSTREAM_DTS     "lavaudio.bitstreamdts"
#define LAVAUDIO_BITSTREAM_DTSHD   "lavaudio.bitstreamdtshd"
#define LAVAUDIO_DTSHDFRAMING      "lavaudio.dtshdframing"
#define LAVAUDIO_AUTOSYNCAV        "lavaudio.autosyncav"
#define LAVAUDIO_EXPANDMONO        "lavaudio.expandmono"
#define LAVAUDIO_EXPAND61          "lavaudio.expand61"
#define LAVAUDIO_51LEGACY          "lavaudio.51legacy"
#define LAVAUDIO_OUTSTANDARD       "lavaudio.outstandard"
#define LAVAUDIO_MIXINGENABLED     "lavaudio.mixingenabled"
#define LAVAUDIO_MIXINGLAYOUT      "lavaudio.mixinglayout"
#define LAVAUDIO_MIXING_DONTMIX    "lavaudio.mixingdontmix"
#define LAVAUDIO_MIXING_NORMALIZE  "lavaudio.mixingnormalize"
#define LAVAUDIO_MIXING_CLIP       "lavaudio.mixingclip"
#define LAVAUDIO_MIXINGMODE        "lavaudio.mixingmode"
#define LAVAUDIO_MIXINGCENTER      "lavaudio.mixingcenter"
#define LAVAUDIO_MIXINGSURROUND    "lavaudio.mixingsurround"
#define LAVAUDIO_MIXINGLFE         "lavaudio.mixinglfe"
#define LAVAUDIO_RESET             "lavaudio.reset"
#define LAVAUDIO_LAYOUT_MONO       "lavaudio.layout.mono"
#define LAVAUDIO_LAYOUT_STEREO     "lavaudio.layout.stereo"
#define LAVAUDIO_LAYOUT_2_2        "lavaudio.layout.2.2"
#define LAVAUDIO_LAYOUT_5POINT1    "lavaudio.layout.5.1"
#define LAVAUDIO_LAYOUT_6POINT1    "lavaudio.layout.6.1"
#define LAVAUDIO_LAYOUT_7POINT1    "lavaudio.layout.7.1"
#define LAVAUDIO_MATRIX_NONE       "lavaudio.matrix.none"
#define LAVAUDIO_MATRIX_DOLBY      "lavaudio.matrix.dolby"
#define LAVAUDIO_MATRIX_DPLII      "lavaudio.matrix.dplii"



using namespace std;

CGUIDialogLAVAudio::CGUIDialogLAVAudio()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_LAVAUDIO, "DialogSettings.xml")
{
}

CGUIDialogLAVAudio::~CGUIDialogLAVAudio()
{ 
}

void CGUIDialogLAVAudio::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(55078);

  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogLAVAudio::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  bool usePopup = CServiceBroker::GetGUI()->GetSkinInfo()->HasSkinFile("DialogSlider.xml");
  
  std::shared_ptr<CSettingCategory> category = AddCategory("dsplayerlavaudio", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupProperty = AddGroup(category);
  if (groupProperty == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> group = AddGroup(category);
  if (group == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupBitstream = AddGroup(category);
  if (groupBitstream == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupOptions = AddGroup(category);
  if (groupOptions == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  const std::shared_ptr<CSettingGroup> groupDRC = AddGroup(category);
  if (groupDRC == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupMixer = AddGroup(category);
  if (groupMixer == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupSettings = AddGroup(category);
  if (groupSettings == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupEncoding = AddGroup(category);
  if (groupEncoding == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupReset = AddGroup(category);
  if (groupReset == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVAudio: unable to setup settings");
    return;
  }

  // Get settings from the current running filter
  IBaseFilter *pBF;
  CGraphFilters::Get()->GetInternalFilter(CGraphFilters::INTERNAL_LAVAUDIO, &pBF);
  CGraphFilters::Get()->GetLavSettings(CGraphFilters::INTERNAL_LAVAUDIO, pBF);

  IntegerSettingOptions entries;
  CLavSettings &lavSettings = CMediaSettings::GetInstance().GetCurrentLavSettings();

  // BUTTON
  AddButton(groupProperty, LAVAUDIO_PROPERTYPAGE, 80013, SettingLevel::Basic);

  // TRAYICON
  AddToggle(group, LAVAUDIO_TRAYICON, 80001, SettingLevel::Basic, lavSettings.audio_bTrayIcon);

  // BITSTREAM
  AddToggle(groupBitstream, LAVAUDIO_BITSTREAM_AC3, 81003, SettingLevel::Basic, lavSettings.audio_bBitstream[0]);
  AddToggle(groupBitstream, LAVAUDIO_BITSTREAM_EAC3, 81004, SettingLevel::Basic, lavSettings.audio_bBitstream[1]);
  AddToggle(groupBitstream, LAVAUDIO_BITSTREAM_TRUEHD, 81005, SettingLevel::Basic, lavSettings.audio_bBitstream[2]);
  AddToggle(groupBitstream, LAVAUDIO_BITSTREAM_DTS, 81006, SettingLevel::Basic, lavSettings.audio_bBitstream[3]);
  AddToggle(groupBitstream, LAVAUDIO_BITSTREAM_DTSHD, 81007, SettingLevel::Basic, lavSettings.audio_bBitstream[4]);
  AddToggle(groupBitstream, LAVAUDIO_DTSHDFRAMING, 81008, SettingLevel::Basic, lavSettings.audio_bDTSHDFraming);

  // OPTIONS
  AddToggle(groupOptions, LAVAUDIO_AUTOSYNCAV, 81009, SettingLevel::Basic, lavSettings.audio_bAutoAVSync);
  AddToggle(groupOptions, LAVAUDIO_OUTSTANDARD, 81010, SettingLevel::Basic, lavSettings.audio_bOutputStandardLayout);
  AddToggle(groupOptions, LAVAUDIO_EXPANDMONO, 81011, SettingLevel::Basic, lavSettings.audio_bExpandMono);
  AddToggle(groupOptions, LAVAUDIO_EXPAND61, 81012, SettingLevel::Basic, lavSettings.audio_bExpand61);
  AddToggle(groupOptions, LAVAUDIO_51LEGACY, 81031, SettingLevel::Basic, lavSettings.audio_b51Legacy);

  // DRC

  // dependencies
  CSettingDependency dependencyDRCEnabled(SettingDependencyType::Enable,GetSettingsManager());
  dependencyDRCEnabled.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(LAVAUDIO_DRCENABLED, "true", SettingDependencyOperator::Equals, false, GetSettingsManager())));
  SettingDependencies depsDRCEnabled;
  depsDRCEnabled.push_back(dependencyDRCEnabled);

  AddToggle(groupDRC, LAVAUDIO_DRCENABLED, 81001, SettingLevel::Basic, lavSettings.audio_bDRCEnabled);

  std::shared_ptr<CSettingInt> settingDRCLevel;
  settingDRCLevel = AddSlider(groupDRC, LAVAUDIO_DRCLEVEL, 81002, SettingLevel::Basic, lavSettings.audio_iDRCLevel, "%i%%", 0, 1, 100);
  settingDRCLevel->SetParent(LAVAUDIO_DRCENABLED);
  settingDRCLevel->SetDependencies(depsDRCEnabled);

  // MIXER

  // dependencies
  CSettingDependency dependencyMixingEnabled(SettingDependencyType::Enable, GetSettingsManager());
  dependencyMixingEnabled.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(LAVAUDIO_MIXINGENABLED, "true", SettingDependencyOperator::Equals, false, GetSettingsManager())));
  SettingDependencies depsMixingEnabled;
  depsMixingEnabled.push_back(dependencyMixingEnabled);

  AddToggle(groupMixer, LAVAUDIO_MIXINGENABLED, 81013, SettingLevel::Basic, lavSettings.audio_bMixingEnabled);
  entries.clear();
  entries.emplace_back(LAVAUDIO_LAYOUT_MONO, (int)AV_CH_LAYOUT_MONO);
  entries.emplace_back(LAVAUDIO_LAYOUT_STEREO, (int)AV_CH_LAYOUT_STEREO);
  entries.emplace_back(LAVAUDIO_LAYOUT_2_2, (int)AV_CH_LAYOUT_2_2);
  entries.emplace_back(LAVAUDIO_LAYOUT_5POINT1, (int)AV_CH_LAYOUT_5POINT1);
  entries.emplace_back(LAVAUDIO_LAYOUT_6POINT1, (int)AV_CH_LAYOUT_6POINT1);
  entries.emplace_back(LAVAUDIO_LAYOUT_7POINT1, (int)AV_CH_LAYOUT_7POINT1);
  
  std::shared_ptr<CSettingInt> settingMixLayout;
  settingMixLayout = AddList(groupMixer, LAVAUDIO_MIXINGLAYOUT, 81014, SettingLevel::Basic, lavSettings.audio_dwMixingLayout, entries, 81014);
  settingMixLayout->SetParent(LAVAUDIO_MIXINGENABLED);
  settingMixLayout->SetDependencies(depsMixingEnabled);

  std::shared_ptr<CSettingNumber> settingMixCenter;
  settingMixCenter = AddSlider(groupMixer, LAVAUDIO_MIXINGCENTER, 81021, SettingLevel::Basic, DWToFloat(lavSettings.audio_dwMixingCenterLevel), "%1.2f", 0.0f, 0.01f, 1.00f);
  settingMixCenter->SetParent(LAVAUDIO_MIXINGENABLED);
  settingMixCenter->SetDependencies(depsMixingEnabled);

  std::shared_ptr<CSettingNumber> settingMixSurround;
  settingMixSurround = AddSlider(groupMixer, LAVAUDIO_MIXINGSURROUND, 81022, SettingLevel::Basic, DWToFloat(lavSettings.audio_dwMixingSurroundLevel), "%1.2f", 0.0f, 0.01f, 1.00f);
  settingMixSurround->SetParent(LAVAUDIO_MIXINGENABLED);
  settingMixSurround->SetDependencies(depsMixingEnabled);

  std::shared_ptr<CSettingNumber> settingMixLFE;
  settingMixLFE = AddSlider(groupMixer, LAVAUDIO_MIXINGLFE, 81023, SettingLevel::Basic, DWToFloat(lavSettings.audio_dwMixingLFELevel), "%1.2f", 0.0f, 0.01f, 1.00f);
  settingMixLFE->SetParent(LAVAUDIO_MIXINGENABLED);
  settingMixLFE->SetDependencies(depsMixingEnabled);


  // SETTINGS
  AddToggle(groupSettings, LAVAUDIO_MIXING_DONTMIX, 81024, SettingLevel::Basic, lavSettings.audio_dwMixingFlags & LAV_MIXING_FLAG_UNTOUCHED_STEREO);
  AddToggle(groupSettings, LAVAUDIO_MIXING_NORMALIZE, 81025, SettingLevel::Basic, lavSettings.audio_dwMixingFlags & LAV_MIXING_FLAG_NORMALIZE_MATRIX);
  AddToggle(groupSettings, LAVAUDIO_MIXING_CLIP, 81026, SettingLevel::Basic, lavSettings.audio_dwMixingFlags & LAV_MIXING_FLAG_CLIP_PROTECTION);

  // ENCODINGS
  entries.clear();
  entries.emplace_back(LAVAUDIO_MATRIX_NONE, MatrixEncoding_None);
  entries.emplace_back(LAVAUDIO_MATRIX_DOLBY, MatrixEncoding_Dolby);
  entries.emplace_back(LAVAUDIO_MATRIX_DPLII, MatrixEncoding_DPLII);
  AddList(groupEncoding, LAVAUDIO_MIXINGMODE, 81027, SettingLevel::Basic, lavSettings.audio_dwMixingMode, entries, 81027);


  // BUTTON RESET
  if (!CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>()->IsPlayingVideo())
    AddButton(groupReset, LAVAUDIO_RESET, 10041, SettingLevel::Basic);
}

void CGUIDialogLAVAudio::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CLavSettings &lavSettings = CMediaSettings::GetInstance().GetCurrentLavSettings();

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);
  const std::string &settingId = setting->GetId();

  if (settingId == LAVAUDIO_TRAYICON)
    lavSettings.audio_bTrayIcon = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_DRCENABLED)
    lavSettings.audio_bDRCEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_DRCLEVEL)
    lavSettings.audio_iDRCLevel = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVAUDIO_BITSTREAM_AC3)
    lavSettings.audio_bBitstream[0] = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_BITSTREAM_EAC3)
    lavSettings.audio_bBitstream[1] = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_BITSTREAM_TRUEHD)
    lavSettings.audio_bBitstream[2] = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_BITSTREAM_DTS)
    lavSettings.audio_bBitstream[3] = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_BITSTREAM_DTSHD)
    lavSettings.audio_bBitstream[4] = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_DTSHDFRAMING)
    lavSettings.audio_bDTSHDFraming = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_AUTOSYNCAV)
    lavSettings.audio_bAutoAVSync = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_EXPANDMONO)
    lavSettings.audio_bExpandMono = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_EXPAND61)
    lavSettings.audio_bExpand61 = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_51LEGACY)
    lavSettings.audio_b51Legacy = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_OUTSTANDARD)
    lavSettings.audio_bOutputStandardLayout = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_MIXINGENABLED)
    lavSettings.audio_bMixingEnabled = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVAUDIO_MIXINGLAYOUT)
    lavSettings.audio_dwMixingLayout = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVAUDIO_MIXING_DONTMIX)
  {
    if (std::static_pointer_cast<const CSettingBool>(setting)->GetValue())
      lavSettings.audio_dwMixingFlags |= LAV_MIXING_FLAG_UNTOUCHED_STEREO;
    else
      lavSettings.audio_dwMixingFlags &= ~LAV_MIXING_FLAG_UNTOUCHED_STEREO;
  }
  if (settingId == LAVAUDIO_MIXING_NORMALIZE)
  {
    if (std::static_pointer_cast<const CSettingBool>(setting)->GetValue())
      lavSettings.audio_dwMixingFlags |= LAV_MIXING_FLAG_NORMALIZE_MATRIX;
    else
      lavSettings.audio_dwMixingFlags &= ~LAV_MIXING_FLAG_NORMALIZE_MATRIX;
  }
  if (settingId == LAVAUDIO_MIXING_CLIP)
  {
    if (std::static_pointer_cast<const CSettingBool>(setting)->GetValue())
      lavSettings.audio_dwMixingFlags |= LAV_MIXING_FLAG_CLIP_PROTECTION;
    else
      lavSettings.audio_dwMixingFlags &= ~LAV_MIXING_FLAG_CLIP_PROTECTION;
  }
  if (settingId == LAVAUDIO_MIXINGMODE)
    lavSettings.audio_dwMixingMode = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVAUDIO_MIXINGCENTER)
    lavSettings.audio_dwMixingCenterLevel = FloatToDw(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
  if (settingId == LAVAUDIO_MIXINGSURROUND)
    lavSettings.audio_dwMixingSurroundLevel = FloatToDw(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
  if (settingId == LAVAUDIO_MIXINGLFE)
    lavSettings.audio_dwMixingLFELevel = FloatToDw(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());

  // Get current running filter
  IBaseFilter *pBF;
  CGraphFilters::Get()->GetInternalFilter(CGraphFilters::INTERNAL_LAVAUDIO, &pBF);

  // Set settings changes into the running filter
  CGraphFilters::Get()->SetLavSettings(CGraphFilters::INTERNAL_LAVAUDIO, pBF);

  // Save new settings into DSPlayer DB
  CGraphFilters::Get()->SaveLavSettings(CGraphFilters::INTERNAL_LAVAUDIO);
}

void CGUIDialogLAVAudio::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingAction(setting);
  const std::string &settingId = setting->GetId();

  if (settingId == LAVAUDIO_PROPERTYPAGE)
  {
    CGraphFilters::Get()->ShowInternalPPage(CGraphFilters::INTERNAL_LAVAUDIO, true);
    this->Close();
  }

  if (settingId == LAVAUDIO_RESET)
  {
    if (!CGUIDialogYesNo::ShowAndGetInput(10041, 10042, 0, 0))
      return;

    CGraphFilters::Get()->EraseLavSetting(CGraphFilters::INTERNAL_LAVAUDIO);
    this->Close();
  }
}


