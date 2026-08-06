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

#include "GUIDialogDSPlayercoreFactory.h"
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
#include "utils/DSFileUtils.h"
#include "ServiceBroker.h"
#include "application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"

#include "guilib/GUIComponent.h"
#include "resources/ResourcesComponent.h"

#define SETTING_RULE_SAVE                   "dsplayercore.save"
#define SETTING_RULE_ADD                    "dsplayercore.add"
#define SETTING_RULE_DEL                    "dsplayercore.del"

using namespace std;

CGUIDialogDSPlayercoreFactory::CGUIDialogDSPlayercoreFactory()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_DSPLAYERCORE, "DialogSettings.xml")
{
  m_dsmanager = CGUIDialogDSManager::Get();
}

CGUIDialogDSPlayercoreFactory::~CGUIDialogDSPlayercoreFactory()
{ }

void CGUIDialogDSPlayercoreFactory::OnInitWindow()
{
  CGUIDialogSettingsManualBase::OnInitWindow();
  m_bEdited = false;
}

void CGUIDialogDSPlayercoreFactory::OnDeinitWindow(int nextWindowID)
{
  if (m_bEdited)
    m_dsmanager->GetNew() ? ActionInternal(SETTING_RULE_ADD) : ActionInternal(SETTING_RULE_SAVE);

  CGUIDialogSettingsManualBase::OnDeinitWindow(nextWindowID);
  ShowDSPlayercoreFactory();
}

void CGUIDialogDSPlayercoreFactory::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(66001);

  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogDSPlayercoreFactory::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  const std::shared_ptr<CSettingCategory> category = AddCategory("dsplayercoresettings", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSPlayercoreFactory: unable to setup settings");
    return;
  }

  // get all necessary setting groups
  const std::shared_ptr<CSettingGroup> group = AddGroup(category);
  if (group == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSPlayercoreFactory: unable to setup settings");
    return;
  }

  const std::shared_ptr<CSettingGroup> groupSave = AddGroup(category);
  if (groupSave == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSPlayercoreFactory: unable to setup settings");
    return;
  }

  if (m_ruleList.size() == 0)
  {
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.name", 60002, "name");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.filetypes", 60003, "filetypes");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.filename", 60004, "filename");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.protocols", 60005, "protocols");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.videoresolution", 60019, "videoresolution");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.videocodec", 60020, "videocodec");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.videoaspect", 66010, "videoaspect");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.audiochannels", 60021, "audiochannels");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.audiocodec", 60022, "audiocodec");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "dsplayrule.mimetypes", 66011, "mimetypes");

    m_dsmanager->InitConfig(m_ruleList, BOOLATTR, "dsplayrule.internetstream", 66003, "internetstream");
    m_dsmanager->InitConfig(m_ruleList, BOOLATTR, "dsplayrule.remote", 66004, "remote");
    m_dsmanager->InitConfig(m_ruleList, BOOLATTR, "dsplayrule.audio", 66005, "audio");
    m_dsmanager->InitConfig(m_ruleList, BOOLATTR, "dsplayrule.video", 66006, "video");
    m_dsmanager->InitConfig(m_ruleList, BOOLATTR, "dsplayrule.dvd", 66007, "dvd");
    m_dsmanager->InitConfig(m_ruleList, BOOLATTR, "dsplayrule.dvdimage", 66008, "dvdimage");
    m_dsmanager->InitConfig(m_ruleList, BOOLATTR, "dsplayrule.dvdfile", 66009, "dvdfile");
  }

  // Reset Button value
  m_dsmanager->ResetValue(m_ruleList);

  // Load userdata Playercorefactory.xml
  if (!m_dsmanager->GetNew())
  {
    TiXmlElement *pRules;
    m_dsmanager->LoadDsXML(PLAYERCOREFACTORY, pRules);

    if (pRules)
    {
      TiXmlElement *pRule = m_dsmanager->KeepSelectedNode(pRules, "rule");

      for (const auto &it : m_ruleList)
      {
        if (it->m_configType == EDITATTR)
          it->m_value = CDSXMLUtils::GetString(pRule, it->m_attr.c_str());

        if (it->m_configType == BOOLATTR)
        {
          it->m_value = CDSXMLUtils::GetString(pRule, it->m_attr.c_str());
          if (it->m_value == "")
            it->m_value = "false";
        }
      }
    }
  }

  // Stamp Button
  for (const auto &it : m_ruleList)
  {
    if (it->m_configType == EDITATTR)
      AddEdit(group, it->m_setting, it->m_label, SettingLevel::Basic, it->m_value, true);

    if (it->m_configType == BOOLATTR)
      AddToggle(group, it->m_setting, it->m_label, SettingLevel::Basic, it->GetBoolValue());
  }

  if (!m_dsmanager->GetNew())
    AddButton(groupSave, SETTING_RULE_DEL, 60017, SettingLevel::Basic);
}

void CGUIDialogDSPlayercoreFactory::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  m_bEdited = true;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);
  const std::string &settingId = setting->GetId();

  for (const auto &it : m_ruleList)
  {
    if (settingId == it->m_setting)
    {
      if (it->m_configType != BOOLATTR)
        it->m_value = std::static_pointer_cast<const CSettingString>(setting)->GetValue();
      else
        it->SetBoolValue(std::static_pointer_cast<const CSettingBool>(setting)->GetValue());
    }
  }
}

void CGUIDialogDSPlayercoreFactory::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  // Init variables
  CGUIDialogSettingsManualBase::OnSettingAction(setting);
  const std::string &settingId = setting->GetId();

  ActionInternal(settingId);
}

void CGUIDialogDSPlayercoreFactory::ActionInternal(const std::string &settingId)
{
  if (settingId == "")
    return;

  // Load userdata playercorefactory.xml
  TiXmlElement *pRules;
  m_dsmanager->LoadDsXML(PLAYERCOREFACTORY, pRules, true);
  if (!pRules)
    return;

  // Del Rule
  if (settingId == SETTING_RULE_DEL)
  {
    if (!CGUIDialogYesNo::ShowAndGetInput(60017, 60018, 0, 0))
      return;

    TiXmlElement *oldRule = m_dsmanager->KeepSelectedNode(pRules, "rule");
    pRules->RemoveChild(oldRule);

    m_dsmanager->SaveDsXML(PLAYERCOREFACTORY);

    CServiceBroker::GetPlayerCoreFactory().OnSettingsLoaded();

    CGUIDialogDSPlayercoreFactory::Close();
  }

  // Add & Save Rule
  if (settingId == SETTING_RULE_ADD || settingId == SETTING_RULE_SAVE)
  {
    TiXmlElement pRule("rule");

    for (const auto &it : m_ruleList)
    {
      if (it->m_configType == EDITATTR && it->m_value != "")
        pRule.SetAttribute(it->m_attr.c_str(), it->m_value.c_str());

      if (it->m_configType == BOOLATTR && it->m_value != "false")
        pRule.SetAttribute(it->m_attr.c_str(), it->m_value.c_str());
    }

    pRule.SetAttribute("player", "VideoPlayer");

    // SAVE
    if (settingId == SETTING_RULE_SAVE)
    {
      TiXmlElement *oldRule = m_dsmanager->KeepSelectedNode(pRules, "rule");
      pRules->ReplaceChild(oldRule, pRule);
    }

    if (settingId == SETTING_RULE_ADD)
      pRules->InsertEndChild(pRule);

    m_bEdited = false;
    m_dsmanager->SaveDsXML(PLAYERCOREFACTORY);
    CServiceBroker::GetPlayerCoreFactory().OnSettingsLoaded();

    CGUIDialogDSPlayercoreFactory::Close();
  }
}

void CGUIDialogDSPlayercoreFactory::ShowDSPlayercoreFactory()
{
  // Load userdata PlayercoreFactory.xml
  TiXmlElement *pRules;
  CGUIDialogDSManager::Get()->LoadDsXML(PLAYERCOREFACTORY, pRules, true);
  if (!pRules)
    return;

  int selected;
  int count = 0;

  CGUIDialogSelect *pDlg = (CGUIDialogSelect *)CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_SELECT);
  if (!pDlg)
    return ;

  pDlg->SetHeading(66001);

  std::vector<std::string> vecAttr
  {
    "name", "filename", "filetypes", "protocols",
    "videocodec", "videoresolution", "videoaspect", "audiocodec", "audiochannels",
    "internetstream", "remote", "audio", "video", "dvd", "dvdimage", "dvdfile", "mimetypes"
  };

  std::string value;
  std::string strLabel;

  TiXmlElement *pRule = pRules->FirstChildElement("rule");
  while (pRule)
  {
    strLabel = "Rule: ";
    value = CDSXMLUtils::GetString(pRule, "name");
    if (value != "")
      strLabel = StringUtils::Format("%s%s ", strLabel.c_str(), value.c_str());

    else
    {
      int countAttr = 0;
      for (const auto &it : vecAttr)
      {

        value = CDSXMLUtils::GetString(pRule, it.c_str());
        if (value != "")
        {
          strLabel = StringUtils::Format("%s %s=%s ", strLabel.c_str(), it.c_str(), value.c_str());
          countAttr++;
        }
        if (countAttr > 1)
          break;
      }
    }

    // Load only VideoPlayer rules
    value = CDSXMLUtils::GetString(pRule, "player");
    StringUtils::ToLower(value);
    if (value == "videoplayer")
      pDlg->Add(strLabel);

    pRule = pRule->NextSiblingElement("rule");
    count++;
  }

  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(66002).c_str());
  pDlg->Open();

  selected = pDlg->GetSelectedItem();
  CGUIDialogDSManager::Get()->SetConfig(selected == count, selected);

  if (selected > -1) 
    CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_DIALOG_DSPLAYERCORE);
}


