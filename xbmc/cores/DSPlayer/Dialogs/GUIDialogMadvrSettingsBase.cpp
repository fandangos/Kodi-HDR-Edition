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

#include "GUIDialogMadvrSettingsBase.h"
#include "ServiceBroker.h"
#include "Application/Application.h"
#include "Application/ApplicationPlayer.h"
#include "application/ApplicationComponents.h"
#include "settings/SettingsComponent.h"
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
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "addons/Skin.h"
#include "DSPlayerDatabase.h"
#include "guilib/GUIComponent.h"
#include "resources/ResourcesComponent.h"

#define SETTING_VIDEO_SAVE                "dsvideo.save"
#define SETTING_VIDEO_LOAD                "dsvideo.load"

using namespace std;

int CGUIDialogMadvrSettingsBase::m_iSectionId = -1;
int CGUIDialogMadvrSettingsBase::m_label = -1;

CGUIDialogMadvrSettingsBase::CGUIDialogMadvrSettingsBase(int windowId, const std::string &xmlFile)
  : CGUIDialogSettingsManualBase(windowId, xmlFile)
{
  m_bMadvr = false;
  m_iSectionIdInternal = -1;
}

CGUIDialogMadvrSettingsBase::~CGUIDialogMadvrSettingsBase()
{
}

void CGUIDialogMadvrSettingsBase::SaveControlStates()
{
  CGUIDialogSettingsManualBase::SaveControlStates();

  int id = GetFocusedControlID();
  if (id == CONTROL_SETTINGS_CANCEL_BUTTON)
    id = CONTROL_SETTINGS_START_CONTROL;

  m_focusPositions[m_iSectionIdInternal] = id;
}

void CGUIDialogMadvrSettingsBase::RestoreControlStates()
{
  CGUIDialogSettingsManualBase::RestoreControlStates();

  const auto &it = m_focusPositions.find(m_iSectionIdInternal);
  if (it != m_focusPositions.end())
    SET_CONTROL_FOCUS(it->second, 0);
}

void CGUIDialogMadvrSettingsBase::InitializeSettings()
{
  
  CGUIDialogSettingsManualBase::InitializeSettings();
  
  m_bMadvr = g_application.GetComponent<CApplicationPlayer>()->UsingDS(DIRECTSHOW_RENDERER_MADVR);
  //&& CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_MANAGEMADVRWITHKODI) > KODIGUI_NEVER;
  //m_bMadvr = g_application.m_pPlayer->UsingDS(DIRECTSHOW_RENDERER_MADVR) && (CSettings::GetInstance().GetInt(CSettings::SETTING_DSPLAYER_MANAGEMADVRWITHKODI) > KODIGUI_NEVER);
  m_iSectionIdInternal = m_iSectionId;

  bool usePopup = CServiceBroker::GetGUI()->GetSkinInfo()->HasSkinFile("DialogSlider.xml");

  m_category = AddCategory("videosettings", -1);
  if (m_category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogMadvrSettings: unable to setup settings");
    return;
  }

  if (!m_bMadvr)
    return;

  if (m_bMadvr && m_iSectionId == MADVR_VIDEO_ROOT)
  {
    std::shared_ptr<CSettingGroup> groupMadvrSave = AddGroup(m_category);
    if (groupMadvrSave == NULL)
    {
      CLog::Log(LOGERROR, "CGUIDialogMadvrSettings: unable to setup settings");
      return;
    }
    
    if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_DSPLAYER_MANAGEMADVRWITHKODI) == KODIGUI_LOAD_DSPLAYER)
    {
      //LOAD SETTINGS...
      AddButton(groupMadvrSave, SETTING_VIDEO_LOAD, 70611, SettingLevel::Basic);

      //SAVE SETTINGS...
      AddButton(groupMadvrSave, SETTING_VIDEO_SAVE, 70600, SettingLevel::Basic);
    }
  }
  std::shared_ptr<CSettingGroup> groupFilters = AddGroup(m_category);
  AddButton(groupFilters, "video.dsfilters", 55062, SettingLevel::Basic);
  std::map<int, std::shared_ptr<CSettingGroup>> groups;
  CMadvrSettings &madvrSettings = CMediaSettings::GetInstance().GetCurrentMadvrSettings();
  g_application.GetComponent<CApplicationPlayer>()->LoadSettings(m_iSectionId);

  for (const auto &it : madvrSettings.m_gui[m_iSectionId])
  {
    std::shared_ptr<CSetting> setting;

    if (groups[it.group] == NULL)
    {
      groups[it.group] = AddGroup(m_category);
      if (groups[it.group] == NULL)
      {
        CLog::Log(LOGERROR, "CGUIDialogMadvrSettings: unable to setup settings");
        return;
      }
    }

    if (it.type.find("button_") != std::string::npos)
    {
      
      setting = AddButton(groups[it.group], it.dialogId, it.label, SettingLevel::Basic);
    }
    else if (it.type == "bool")
    {
      setting = AddToggle(groups[it.group], it.dialogId, it.label, SettingLevel::Basic, madvrSettings.m_db[it.name].asBoolean());
    }
    else if (it.type == "float")
    {
      setting = AddSlider(groups[it.group], it.dialogId, it.label, SettingLevel::Basic, madvrSettings.m_db[it.name].asFloat(),
        it.slider.format, it.slider.min, it.slider.step, it.slider.max, it.slider.parentLabel, usePopup);
    }
    else if (it.type.find("list_") != std::string::npos)
    {
      CLog::Log(LOGERROR, "CGUIDialogMadvrSettings: TODO");
#if 1
      if (!it.optionsInt.empty())
        setting = AddList(groups[it.group], it.dialogId, it.label, SettingLevel::Basic, madvrSettings.m_db[it.name].asInteger(), it.optionsInt, it.label);
      else
        setting = AddList(groups[it.group], it.dialogId, it.label, SettingLevel::Basic, madvrSettings.m_db[it.name].asString(), MadvrSettingsOptionsString, it.label);
#endif
    }

    if (!it.dependencies.empty() && setting)
      g_application.GetComponent<CApplicationPlayer>()->AddDependencies(it.dependencies, GetSettingsManager(), setting.get());

    if (!it.parent.empty() && setting)
      setting->SetParent(it.parent);
  }
}

void CGUIDialogMadvrSettingsBase::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  if (!m_bMadvr)
    return;

  g_application.GetComponent<CApplicationPlayer>()->OnSettingChanged(m_iSectionId, GetSettingsManager(), setting.get());
}

void CGUIDialogMadvrSettingsBase::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingAction(setting);

  if (!m_bMadvr)
    return;

  CMadvrSettings &madvrSettings = CMediaSettings::GetInstance().GetCurrentMadvrSettings();

  const std::string &settingId = setting->GetId();

  auto it = std::find_if(madvrSettings.m_gui[m_iSectionId].begin(), madvrSettings.m_gui[m_iSectionId].end(),
    [settingId](const MadvrListSettings setting){
    return setting.dialogId == settingId;
  });

  if (it != madvrSettings.m_gui[m_iSectionId].end())
  {
    if (it->type == "button_section")
    {
      if (m_iSectionId == MADVR_VIDEO_ROOT)
      {
        SetSection(it->sectionId, it->label);
        CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_DIALOG_MADVR);
      }
      else
      {
        SetSection(it->sectionId, it->label);
        SaveControlStates();
        Close();
        Open();
      }
    }
    else if (it->type == "button_debug")
    {
      g_application.GetComponent<CApplicationPlayer>()->ListSettings(it->value);
    }
  }

  if (settingId == SETTING_VIDEO_SAVE)
    SaveMadvrSettings();
  else if (settingId == SETTING_VIDEO_LOAD)
    LoadMadvrSettings();
}

void CGUIDialogMadvrSettingsBase::LoadMadvrSettings()
{
  CGUIDialogSelect *pDlg = (CGUIDialogSelect *)CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_SELECT);
  if (!pDlg)
    return;

  CMadvrSettings &madvrSettings = CMediaSettings::GetInstance().GetCurrentMadvrSettings();

  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70616).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70617).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70618).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70612).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70613).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70614).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70615).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70619).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70620).c_str());

  pDlg->SetHeading(70611);
  pDlg->Open();

  if (pDlg->GetSelectedItem() < 0)
    return;

  int selected = -1;
  int userId = 0;
  std::string strSelected = pDlg->GetSelectedFileItem()->GetLabel();

  //SD
  if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70612))
  {
    selected = MADVR_RES_SD;
  }
  //720
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70613))
  {
    selected = MADVR_RES_720;
  }
  //1080
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70614))
  {
    selected = MADVR_RES_1080;
  }
  //2160
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70615))
  {
    selected = MADVR_RES_2160;
  }
  //USER1
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70616))
  {
    selected = MADVR_RES_USER;
    userId = 1;
  }
  //USER2
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70617))
  {
    selected = MADVR_RES_USER;
    userId = 2;
  }
  //USER3
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70618))
  {
    selected = MADVR_RES_USER;
    userId = 3;
  }
  //DEFAULT
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70619))
  {
    selected = MADVR_RES_DEFAULT;
  }
  //RESTORE ATSTART SETTINGS
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70620))
  {
    selected = MADVR_RES_ATSTART;
  }
  if (selected > -1)
  {
    CDSPlayerDatabase dspdb;
    if (!dspdb.Open())
      return;

    if (selected == MADVR_RES_DEFAULT)
    {
      CMediaSettings::GetInstance().GetCurrentMadvrSettings().RestoreDefaultSettings();
      g_application.GetComponent<CApplicationPlayer>()->RestoreSettings();
      Close();
    }
    else if (selected == MADVR_RES_USER)
    {
      dspdb.GetUserSettings(userId, madvrSettings);
      g_application.GetComponent<CApplicationPlayer>()->RestoreSettings();
      Close();
    }
    else if (selected == MADVR_RES_ATSTART)
    {
      madvrSettings.RestoreAtStartSettings();
      g_application.GetComponent<CApplicationPlayer>()->RestoreSettings();
      Close();
    }
    else
    {
      dspdb.GetResSettings(selected, madvrSettings);
      g_application.GetComponent<CApplicationPlayer>()->RestoreSettings();
      Close();
    }
    dspdb.Close();
  }
}

void CGUIDialogMadvrSettingsBase::SaveMadvrSettings()
{
  CGUIDialogSelect *pDlg = (CGUIDialogSelect *)CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_SELECT);
  if (!pDlg)
    return;

  CMadvrSettings &madvrSettings = CMediaSettings::GetInstance().GetCurrentMadvrSettings();

  if (!madvrSettings.m_TvShowName.empty())
    pDlg->Add(StringUtils::Format(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70605).c_str(), madvrSettings.m_TvShowName.c_str()));

  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70601).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70602).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70603).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70604).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70606).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70608).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70609).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70610).c_str());
  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70607).c_str());

  pDlg->SetHeading(70600);
  pDlg->Open();

  if (pDlg->GetSelectedItem() < 0)
    return;

  int label;
  int selected = -1;
  int userId = 0;
  std::string strSelected = pDlg->GetSelectedFileItem()->GetLabel();

  //TVSHOW
  if (strSelected == StringUtils::Format(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70605).c_str(), madvrSettings.m_TvShowName.c_str()))
  {
    selected = MADVR_RES_TVSHOW;
    label = 70605;
  }
  //SD
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70601))
  {
    selected = MADVR_RES_SD;
    label = 70601;
  }
  //720
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70602))
  {
    selected = MADVR_RES_720;
    label = 70602;
  }
  //1080
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70603))
  {
    selected = MADVR_RES_1080;
    label = 70603;
  }
  //2160
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70604))
  {
    selected = MADVR_RES_2160;
    label = 70604;
  }
  //ALL
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70606))
  {
    selected = MADVR_RES_ALL;
    label = 70606;
  }
  //RESET TO DEFAULT
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70607))
  {
    selected = MADVR_RES_DEFAULT;
    label = 70607;
  }
  //USER1
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70608))
  {
    selected = MADVR_RES_USER;
    label = 70608;
    userId = 1;
  }
  //USER2
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70609))
  {
    selected = MADVR_RES_USER;
    label = 70609;
    userId = 2;
  }
  //USER3
  else if (strSelected == CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(70610))
  {
    selected = MADVR_RES_USER;
    label = 70610;
    userId = 3;
  }

  if (selected > -1)
  {
    if (CGUIDialogYesNo::ShowAndGetInput(StringUtils::Format(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(label).c_str(), madvrSettings.m_TvShowName.c_str()), 750, 0, 12377))
    {
      CDSPlayerDatabase dspdb;
      if (!dspdb.Open())
        return;

      if (selected == MADVR_RES_TVSHOW)
      {
        dspdb.EraseTvShowSettings(madvrSettings.m_TvShowName.c_str());
        dspdb.SetTvShowSettings(madvrSettings.m_TvShowName.c_str(), madvrSettings);
      }
      else if (selected == MADVR_RES_ALL)
      {
        dspdb.EraseVideoSettings();
        dspdb.SetResSettings(selected, madvrSettings);
      }
      else if (selected == MADVR_RES_DEFAULT)
      {
        dspdb.EraseVideoSettings();
        dspdb.EraseUserSettings(1);
        dspdb.EraseUserSettings(2);
        dspdb.EraseUserSettings(3);
        CMediaSettings::GetInstance().GetCurrentMadvrSettings().RestoreDefaultSettings();
        g_application.GetComponent<CApplicationPlayer>()->RestoreSettings();
        Close();
      }
      else if (selected == MADVR_RES_USER)
      {
        dspdb.EraseUserSettings(userId);
        dspdb.SetUserSettings(userId, madvrSettings);
      }
      else
      {
        dspdb.EraseResSettings(selected);
        dspdb.SetResSettings(selected, madvrSettings);
      }
      dspdb.Close();
    }
  }
}

//void CGUIDialogMadvrSettingsBase::MadvrSettingsOptionsString(const CSetting *setting, std::vector< std::pair<std::string, std::string> > &list, std::string &current, void *data)
void CGUIDialogMadvrSettingsBase::MadvrSettingsOptionsString(const std::shared_ptr<const CSetting>& setting,
  StringSettingOptions& list,
  std::string& current,
  void* data)
{
  CMadvrSettings &madvrSettings = CMediaSettings::GetInstance().GetCurrentMadvrSettings();

  const std::string &settingId = setting->GetId();

  auto it = std::find_if(madvrSettings.m_gui[m_iSectionId].begin(), madvrSettings.m_gui[m_iSectionId].end(),
    [settingId](const MadvrListSettings setting){
    return setting.dialogId == settingId;
  });

  if (it != madvrSettings.m_gui[m_iSectionId].end())
  {
    for (const auto &option : it->optionsString)
      list.emplace_back(option);
  }
}