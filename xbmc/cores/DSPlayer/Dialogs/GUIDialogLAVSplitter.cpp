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

#include "GUIDialogLAVSplitter.h"
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
#include "addons/Skin.h"
#include "GraphFilters.h"
#include "utils/CharsetConverter.h"
#include <application/ApplicationPlayer.h>
#include "ServiceBroker.h"
#include "guilib/GUIComponent.h"

#define LAVSPLITTER_PROPERTYPAGE      "lavsplitter.propertypage"
#define LAVSPLITTER_TRAYICON          "lavsplitter.trayicon"
#define LAVSPLITTER_PREFAUDIOLANG     "lavsplitter.prefaudiolang"
#define LAVSPLITTER_PREFSUBLANG       "lavsplitter.prefsublang"
#define LAVSPLITTER_PREFSUBADVANCED   "lavsplitter.prefsubadcanced"
#define LAVSPLITTER_SUBMODE           "lavsplitter.submode"
#define LAVSPLITTER_PGSFORCEDSTREAM   "lavsplitter.pgsforcedstream"
#define LAVSPLITTER_PGSONLYFORCED     "lavsplitter.pgsonlyforced"
#define LAVSPLITTER_IVC1MODE          "lavsplitter.ivc1mode"
#define LAVSPLITTER_MATROSKAEXTERNAL  "lavsplitter.matroskaexternal"
#define LAVSPLITTER_SUBSTREAM         "lavsplitter.substream"
#define LAVSPLITTER_REMAUDIOSTREAM    "lavsplitter.remaudiostream"
#define LAVSPLITTER_PREFHQAUDIO       "lavsplitter.prefhqaudio"
#define LAVSPLITTER_IMPAIREDAUDIO     "lavsplitter.impairedaudio"
#define LAVSPLITTER_RESET             "lavsplitter.reset"
#define LAVSPLITTER_NOSUBS            "lavsplitter.sub.nosubs"
#define LAVSPLITTER_FORCEDONLY        "lavsplitter.sub.forcedonly"
#define LAVSPLITTER_DEFAULT           "lavsplitter.sub.default"
#define LAVSPLITTER_ADVANCED          "lavsplitter.sub.advanced"

using namespace std;

CGUIDialogLAVSplitter::CGUIDialogLAVSplitter()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_LAVSPLITTER, "DialogSettings.xml")
{
}

CGUIDialogLAVSplitter::~CGUIDialogLAVSplitter()
{ 
}

void CGUIDialogLAVSplitter::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(55079);

  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogLAVSplitter::InitializeSettings()
{
#if 1
  CGUIDialogSettingsManualBase::InitializeSettings();

  bool usePopup = CServiceBroker::GetGUI()->GetSkinInfo()->HasSkinFile("DialogSlider.xml");

  std::shared_ptr<CSettingCategory> category = AddCategory("dsplayerlavsplitter", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupProperty = AddGroup(category);
  if (groupProperty == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> group = AddGroup(category);
  if (group == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupPreflang = AddGroup(category);
  if (groupPreflang == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupSubmode = AddGroup(category);
  if (groupSubmode == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupBluraysub = AddGroup(category);
  if (groupBluraysub == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupFormat = AddGroup(category);
  if (groupFormat == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupDemuxer = AddGroup(category);
  if (groupDemuxer == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
    return;
  }
  std::shared_ptr<CSettingGroup> groupQueueNet = AddGroup(category);
  if (groupQueueNet == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLAVSplitter: unable to setup settings");
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
  CGraphFilters::Get()->GetInternalFilter(CGraphFilters::INTERNAL_LAVSPLITTER, &pBF);
  CGraphFilters::Get()->GetLavSettings(CGraphFilters::INTERNAL_LAVSPLITTER, pBF);

  std::vector<IntegerSettingOption> entries;
  CLavSettings &lavSettings = CMediaSettings::GetInstance().GetCurrentLavSettings();

  // BUTTON
  AddButton(groupProperty, LAVSPLITTER_PROPERTYPAGE, 80013, SettingLevel::Basic);

  // TRAYICON
  AddToggle(group, LAVSPLITTER_TRAYICON, 80001, SettingLevel::Basic, lavSettings.splitter_bTrayIcon);

  // PREFLANG

  // dependencies
  CSettingDependency dependencyPrefSubLangVisible(SettingDependencyType::Visible, GetSettingsManager());
  dependencyPrefSubLangVisible.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(LAVSPLITTER_SUBMODE, "3", SettingDependencyOperator::Equals, true, GetSettingsManager())));
  SettingDependencies depsPrefSubLangVisible;
  depsPrefSubLangVisible.push_back(dependencyPrefSubLangVisible);

  CSettingDependency dependencyPrefSubAdvVisible(SettingDependencyType::Visible, GetSettingsManager());
  dependencyPrefSubAdvVisible.Or()
    ->Add(CSettingDependencyConditionPtr(new CSettingDependencyCondition(LAVSPLITTER_SUBMODE, "3", SettingDependencyOperator::Equals, false, GetSettingsManager())));
  SettingDependencies depsPrefSubAdvVisible;
  depsPrefSubAdvVisible.push_back(dependencyPrefSubAdvVisible);

  std::string str;
  g_charsetConverter.wToUTF8(lavSettings.splitter_prefAudioLangs, str, false);
  AddEdit(groupPreflang, LAVSPLITTER_PREFAUDIOLANG, 82001, SettingLevel::Basic, str, true);
  
  g_charsetConverter.wToUTF8(lavSettings.splitter_prefSubLangs , str, false);
  std::shared_ptr<CSetting> settingPrefSubLang;
  settingPrefSubLang = AddEdit(groupPreflang, LAVSPLITTER_PREFSUBLANG, 82002, SettingLevel::Basic, str, true);
  settingPrefSubLang->SetDependencies(depsPrefSubLangVisible);

  g_charsetConverter.wToUTF8(lavSettings.splitter_subtitleAdvanced, str, false);
  std::shared_ptr<CSetting>  settingPrefSubAdv;
  settingPrefSubAdv = AddEdit(groupPreflang, LAVSPLITTER_PREFSUBADVANCED, 82016, SettingLevel::Basic, str, true);
  settingPrefSubAdv->SetDependencies(depsPrefSubAdvVisible);

  //SUBMODE
  entries.clear();
  entries.emplace_back(LAVSPLITTER_NOSUBS, (int)LAVSubtitleMode_NoSubs);
  entries.emplace_back(LAVSPLITTER_FORCEDONLY, (int)LAVSubtitleMode_ForcedOnly);
  entries.emplace_back(LAVSPLITTER_DEFAULT, (int)LAVSubtitleMode_Default);
  entries.emplace_back(LAVSPLITTER_ADVANCED, (int)LAVSubtitleMode_Advanced);
  AddList(groupSubmode, LAVSPLITTER_SUBMODE, 82003, SettingLevel::Basic, lavSettings.splitter_subtitleMode, entries, 82003);

  //BLURAYSUB
  AddToggle(groupBluraysub, LAVSPLITTER_PGSFORCEDSTREAM, 82008, SettingLevel::Basic, lavSettings.splitter_bPGSForcedStream);
  AddToggle(groupBluraysub, LAVSPLITTER_PGSONLYFORCED, 82009, SettingLevel::Basic, lavSettings.splitter_bPGSOnlyForced);

  //FORMAT
  AddToggle(groupFormat, LAVSPLITTER_IVC1MODE, 82010, SettingLevel::Basic, lavSettings.splitter_iVC1Mode);
  AddToggle(groupFormat, LAVSPLITTER_MATROSKAEXTERNAL, 82011, SettingLevel::Basic, lavSettings.splitter_bMatroskaExternalSegments);

  //DEMUXER
  AddToggle(groupDemuxer, LAVSPLITTER_SUBSTREAM, 82012, SettingLevel::Basic, lavSettings.splitter_bSubstreams);
  AddToggle(groupDemuxer, LAVSPLITTER_REMAUDIOSTREAM, 82013, SettingLevel::Basic, lavSettings.splitter_bStreamSwitchRemoveAudio);
  AddToggle(groupDemuxer, LAVSPLITTER_PREFHQAUDIO, 82014, SettingLevel::Basic, lavSettings.splitter_bPreferHighQualityAudio);
  AddToggle(groupDemuxer, LAVSPLITTER_IMPAIREDAUDIO, 82015, SettingLevel::Basic, lavSettings.splitter_bImpairedAudio);

  // BUTTON RESET
  if (!g_application.GetComponent<CApplicationPlayer>()->IsPlayingVideo())
    AddButton(groupReset, LAVSPLITTER_RESET, 10041, SettingLevel::Basic);
#endif
}

void CGUIDialogLAVSplitter::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CLavSettings &lavSettings = CMediaSettings::GetInstance().GetCurrentLavSettings();

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);
  const std::string &settingId = setting->GetId();

  std::wstring strW;

  if (settingId == LAVSPLITTER_TRAYICON)
    lavSettings.splitter_bTrayIcon = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVSPLITTER_PREFAUDIOLANG)
  { 
    g_charsetConverter.utf8ToW(std::static_pointer_cast<const CSettingString>(setting)->GetValue(),strW,false);
    lavSettings.splitter_prefAudioLangs = strW;
  }
  if (settingId == LAVSPLITTER_PREFSUBLANG)
  {
    g_charsetConverter.utf8ToW(std::static_pointer_cast<const CSettingString>(setting)->GetValue(), strW, false);
    lavSettings.splitter_prefSubLangs = strW;
  }
  if (settingId == LAVSPLITTER_PREFSUBADVANCED)
  {
    g_charsetConverter.utf8ToW(std::static_pointer_cast<const CSettingString>(setting)->GetValue(), strW, false);
    lavSettings.splitter_subtitleAdvanced = strW;
  }
  if (settingId == LAVSPLITTER_SUBMODE)
    lavSettings.splitter_subtitleMode = (LAVSubtitleMode)std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  if (settingId == LAVSPLITTER_PGSFORCEDSTREAM)
    lavSettings.splitter_bPGSForcedStream = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVSPLITTER_PGSONLYFORCED)
    lavSettings.splitter_bPGSOnlyForced = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVSPLITTER_IVC1MODE)
    lavSettings.splitter_iVC1Mode = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVSPLITTER_MATROSKAEXTERNAL)
    lavSettings.splitter_bMatroskaExternalSegments = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVSPLITTER_SUBSTREAM)
    lavSettings.splitter_bSubstreams = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVSPLITTER_REMAUDIOSTREAM)
    lavSettings.splitter_bStreamSwitchRemoveAudio = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVSPLITTER_PREFHQAUDIO)
    lavSettings.splitter_bPreferHighQualityAudio = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  if (settingId == LAVSPLITTER_IMPAIREDAUDIO)
    lavSettings.splitter_bImpairedAudio = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();

  // Get current running filter
  IBaseFilter *pBF;
  CGraphFilters::Get()->GetInternalFilter(CGraphFilters::INTERNAL_LAVSPLITTER, &pBF);

  // Set settings changes into the running filter
  CGraphFilters::Get()->SetLavSettings(CGraphFilters::INTERNAL_LAVSPLITTER, pBF);

  // Save new settings into DSPlayer DB
  CGraphFilters::Get()->SaveLavSettings(CGraphFilters::INTERNAL_LAVSPLITTER);
}

void CGUIDialogLAVSplitter::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingAction(setting);
  const std::string &settingId = setting->GetId();

  if (settingId == LAVSPLITTER_PROPERTYPAGE)
  {
    CGraphFilters::Get()->ShowInternalPPage(CGraphFilters::INTERNAL_LAVSPLITTER, true);
    this->Close();
  }

  if (settingId == LAVSPLITTER_RESET)
  {
    if (!CGUIDialogYesNo::ShowAndGetInput(10041, 10042, 0, 0))
      return;

    CGraphFilters::Get()->EraseLavSetting(CGraphFilters::INTERNAL_LAVSPLITTER);
    this->Close();
  }
}

