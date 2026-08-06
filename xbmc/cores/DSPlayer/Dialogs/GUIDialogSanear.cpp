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

#include "GUIDialogSanear.h"
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
#include "settings/SettingsComponent.h"
#include "addons/Skin.h"
#include "GraphFilters.h"
#include "FGLoader.h"
#include "ServiceBroker.h"
#include "guilib/GUIComponent.h"

#define SANEAR_PROPERTYPAGE      "sanear.propertypage"
#define SANEAR_DEVICES           "sanear.devices"
#define SANEAR_EXCLUSIVE         "sanear.exclusive"
#define SANEAR_ALLOWBITSTREAM    "sanear.allowbitstream"

#define SANEAR_CROSSFEED         "sanear.crossfeed"
#define SANEAR_IGNORESYSCHANMIX  "sanear.ignoresystemchannelmixer"
#define SANEAR_CMOY              "sanear.cmoy"
#define SANEAR_JMEIER            "sanear.jmeier"
#define SANEAR_CUTOFF            "sanear.cutoff"
#define SANEAR_LEVEL             "sanear.level"

using namespace std;

CGUIDialogSanear::CGUIDialogSanear()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_SANEAR, "DialogSettings.xml")
{
}

CGUIDialogSanear::~CGUIDialogSanear()
{
}

void CGUIDialogSanear::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(55123);

  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogSanear::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  bool usePopup = CServiceBroker::GetGUI()->GetSkinInfo()->HasSkinFile("DialogSlider.xml");

  std::shared_ptr<CSettingCategory> category = AddCategory("dsplayerlavaudio", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogSanear: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupProperty = AddGroup(category);
  if (groupProperty == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogSanear: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> group = AddGroup(category);
  if (group == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogSanear: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupBitstream = AddGroup(category);
  if (groupBitstream == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogSanear: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupOptions = AddGroup(category);
  if (groupOptions == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogSanear: unable to setup settings");
    return;
  }

  std::string sValue;
  bool bValue;
  int iValue;

  // BUTTON
  AddButton(groupProperty, SANEAR_PROPERTYPAGE, 80013, SettingLevel::Basic);
#if 0
  // DEVICES
  sValue = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_SANEARDEVICES);
  AddList(groupBitstream, SANEAR_DEVICES, 55120, 0, sValue, CFGLoader::SettingOptionsSanearDevicesFiller, 81027);
 
  // BITSTREAM

  // dependencies
  std::shared_ptr<CSetting> Dependency dependencySanearExclusiveEnabled(SettingDependencyTypeEnable, GetSettingsManager());
  dependencySanearExclusiveEnabled.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(SANEAR_EXCLUSIVE, "true", SettingDependencyOperatorEquals, false, GetSettingsManager())));
  SettingDependencies depsSanearExclusiveEnabled;
  depsSanearExclusiveEnabled.push_back(dependencySanearExclusiveEnabled);

  bValue = CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SANEAREXCLUSIVE);
  AddToggle(groupBitstream, SANEAR_EXCLUSIVE, 55121, SettingLevel::Basic, bValue);
  bValue = CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SANEARALLOWBITSTREAM);
  std::shared_ptr<CSetting>  *settingSanearAllowBitStream;
  settingSanearAllowBitStream = AddToggle(groupBitstream, SANEAR_ALLOWBITSTREAM, 55122, SettingLevel::Basic, bValue);
  settingSanearAllowBitStream->SetDependencies(depsSanearExclusiveEnabled);

  // IGNORESYSTEMCHANNELMIXER
  bValue = CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SANEARIGNORESYSTEMCHANNELMIXER);
  AddToggle(groupBitstream, SANEAR_IGNORESYSCHANMIX, 55133, SettingLevel::Basic, bValue);

  // STEREOCROSSFEED

  // dependencies
  std::shared_ptr<CSetting> Dependency dependencySanearStereoCFEnabled(SettingDependencyTypeEnable, GetSettingsManager());
  dependencySanearStereoCFEnabled.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(SANEAR_CROSSFEED, "true", SettingDependencyOperatorEquals, false, GetSettingsManager())));
  SettingDependencies depsSanearStereoCFEnabled;
  depsSanearStereoCFEnabled.push_back(dependencySanearStereoCFEnabled);

  bValue = CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_DSPLAYER_SANEARSTEREOCROSSFEED);
  AddToggle(groupOptions, SANEAR_CROSSFEED, 55124, SettingLevel::Basic, bValue);

  std::shared_ptr<CSetting>  *settingSanearCMOY;
  settingSanearCMOY = AddButton(groupOptions, SANEAR_CMOY, 55125, SettingLevel::Basic);
  settingSanearCMOY->SetDependencies(depsSanearStereoCFEnabled);
  
  std::shared_ptr<CSetting>  *settingSanearJmeier;
  settingSanearJmeier = AddButton(groupOptions, SANEAR_JMEIER, 55126, SettingLevel::Basic);
  settingSanearJmeier->SetDependencies(depsSanearStereoCFEnabled);

  iValue = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_SANEARCUTOFF);
  std::shared_ptr<CSetting>  *settingSanearCutOff;
  settingSanearCutOff = AddSlider(groupOptions, SANEAR_CUTOFF, 55127, SettingLevel::Basic, iValue, "%i Hz", SaneAudioRenderer::ISettings::CROSSFEED_CUTOFF_FREQ_MIN, 1, SaneAudioRenderer::ISettings::CROSSFEED_CUTOFF_FREQ_MAX);
  settingSanearCutOff->SetDependencies(depsSanearStereoCFEnabled);

  iValue = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_SANEARLEVEL);
  std::shared_ptr<CSetting>  *settingSanearLevel;
  settingSanearLevel = AddSlider(groupOptions, SANEAR_LEVEL, 55128, SettingLevel::Basic, iValue, "%i dB", SaneAudioRenderer::ISettings::CROSSFEED_LEVEL_MIN, 1, SaneAudioRenderer::ISettings::CROSSFEED_LEVEL_MAX);
  settingSanearLevel->SetDependencies(depsSanearStereoCFEnabled);
#endif
}

void CGUIDialogSanear::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CLavSettings &lavSettings = CMediaSettings::GetInstance().GetCurrentLavSettings();

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);
  const std::string &settingId = setting->GetId();

  std::string sValue;
  bool bValue;
  int iValue;

  if (settingId == SANEAR_DEVICES)
  {
    sValue = std::static_pointer_cast<const CSettingString>(setting)->GetValue();
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetString(CSettings::SETTING_DSPLAYER_SANEARDEVICES, sValue);
  }
  if (settingId == SANEAR_EXCLUSIVE)
  {
    bValue = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetBool(CSettings::SETTING_DSPLAYER_SANEAREXCLUSIVE, bValue);
  }
  if (settingId == SANEAR_ALLOWBITSTREAM)
  {
    bValue = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetBool(CSettings::SETTING_DSPLAYER_SANEARALLOWBITSTREAM, bValue);
  }
  if (settingId == SANEAR_IGNORESYSCHANMIX)
  {
    bValue = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetBool(CSettings::SETTING_DSPLAYER_SANEARIGNORESYSTEMCHANNELMIXER, bValue);
  }
  if (settingId == SANEAR_CROSSFEED)
  {
    bValue = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetBool(CSettings::SETTING_DSPLAYER_SANEARALLOWBITSTREAM, bValue);
  }
  if (settingId == SANEAR_CUTOFF)
  {
    iValue = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetInt(CSettings::SETTING_DSPLAYER_SANEARCUTOFF, iValue);
  }
  if (settingId == SANEAR_LEVEL)
  {
    iValue = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
    CServiceBroker::GetSettingsComponent()->GetSettings()->SetInt(CSettings::SETTING_DSPLAYER_SANEARLEVEL, iValue);;
  }
}

void CGUIDialogSanear::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  int iValue;

  CGUIDialogSettingsManualBase::OnSettingAction(setting);
  const std::string &settingId = setting->GetId();

  if (settingId == SANEAR_PROPERTYPAGE)
  {
    CGraphFilters::Get()->ShowInternalPPage(CGraphFilters::INTERNAL_SANEAR, true);
    this->Close();
  }
  else if (settingId == SANEAR_CMOY)
  {
    
    GetSettingsManager()->SetInt(SANEAR_CUTOFF, SaneAudioRenderer::ISettings::CROSSFEED_CUTOFF_FREQ_CMOY);
    GetSettingsManager()->SetInt(SANEAR_LEVEL, SaneAudioRenderer::ISettings::CROSSFEED_LEVEL_CMOY);
  }
  else if (settingId == SANEAR_JMEIER)
  {
    GetSettingsManager()->SetInt(SANEAR_CUTOFF, SaneAudioRenderer::ISettings::CROSSFEED_CUTOFF_FREQ_JMEIER);
    GetSettingsManager()->SetInt(SANEAR_LEVEL, SaneAudioRenderer::ISettings::CROSSFEED_LEVEL_JMEIER);
  }
}


