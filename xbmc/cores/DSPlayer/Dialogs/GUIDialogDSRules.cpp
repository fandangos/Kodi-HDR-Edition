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

#include "GUIDialogDSRules.h"
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
#include "input/keyboard/Key.h"
#include "cores/DSPlayer/Utils/DSFilterEnumerator.h"
#include "utils/XMLUtils.h"
#include "Filters/RendererSettings.h"
#include "PixelShaderList.h"
#include "utils/DSFileUtils.h"
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/dialogs/GUIDialogSettingsManualBase.h"
#include "ServiceBroker.h"
#include "application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "guilib/GUIComponent.h"
#include "resources/ResourcesComponent.h"

#define SETTING_RULE_SAVE                     "rule.save"
#define SETTING_RULE_ADD                      "rule.add"
#define SETTING_RULE_DEL                      "rule.del"

using namespace std;

CGUIDialogDSRules::CGUIDialogDSRules()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_DSRULES, "DialogSettings.xml")
{
  m_dsmanager = CGUIDialogDSManager::Get();
  m_allowchange = true;
}


CGUIDialogDSRules::~CGUIDialogDSRules()
{ }

void CGUIDialogDSRules::OnInitWindow()
{
  CGUIDialogSettingsManualBase::OnInitWindow();
  HideUnused();
  m_bEdited = false;
}

void CGUIDialogDSRules::OnDeinitWindow(int nextWindowID)
{
  if (m_bEdited)
    m_dsmanager->GetNew() ? ActionInternal (SETTING_RULE_ADD) : ActionInternal (SETTING_RULE_SAVE);

  CGUIDialogSettingsManualBase::OnDeinitWindow(nextWindowID);
  ShowDSRulesList();
}

bool CGUIDialogDSRules::Save()
{
  return true;
}

void CGUIDialogDSRules::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(60001);

  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogDSRules::SetVisible(std::string id, bool visible, ConfigType subType, bool isChild /* = false */)
{
  const std::shared_ptr<CSettings> settings = CServiceBroker::GetSettingsComponent()->GetSettings();
  CSettingsManager* settingsMgr = settings->GetSettingsManager();
  std::shared_ptr<CSetting> setting = settingsMgr->GetSetting(id);
  if (setting->IsVisible() && visible)
    return;
  setting->SetVisible(visible);
  setting->SetEnabled(visible);
  if (!isChild)
    settingsMgr->SetString(id, "[null]");
  else
  { 
    if (subType == SPINNERATTRSHADER)
      settingsMgr->SetString(id, "prescale");
    else
      settingsMgr->SetString(id, "");
  }
}

void CGUIDialogDSRules::HideUnused()
{
  if (!m_allowchange)
    return;

  m_allowchange = false;

  HideUnused(EXTRAFILTER, EDITATTREXTRA);
  HideUnused(SHADER, EDITATTRSHADER);
  HideUnused(SHADER, SPINNERATTRSHADER);

  m_allowchange = true;
}

void CGUIDialogDSRules::HideUnused(ConfigType type, ConfigType subType)
{
  int count = 0;
  bool show;
  
  bool isMadvr = (StringUtils::ToLower(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_VIDEORENDERER)) == "madvr");

  for (const auto &it : m_ruleList)
  {
    if (it->m_configType == type)
    {
      if (it->m_value == "[null]")
        count++;

      show = (count > 1 && it->m_value == "[null]");
      SetVisible(it->m_setting.c_str(), !show, subType);

      show = (count > 0 && it->m_value == "[null]");
      for (const auto &itchild : m_ruleList)
      {
        if (itchild->m_configType == subType && it->m_subNode == itchild->m_subNode)
        {
          if (!isMadvr && subType == SPINNERATTRSHADER)
            SetVisible(itchild->m_setting.c_str(), false, subType, true);
          else
            SetVisible(itchild->m_setting.c_str(), !show, subType, true);
        }
      }
    }
  }
}

bool CGUIDialogDSRules::NodeHasAttr(TiXmlElement *pNode, std::string attr)
{
  if (pNode)
  {
    std::string value = "";
    value = CDSXMLUtils::GetString(pNode, attr.c_str());
    return (value != "");
  }

  return false;
}

void CGUIDialogDSRules::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  std::shared_ptr<CSettingCategory> category = AddCategory("dsrulesettings", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSRules: unable to setup settings");
    return;
  }

  // get all necessary setting groups
  std::shared_ptr<CSettingGroup> groupPriority = AddGroup(category);
  if (groupPriority == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSRules: unable to setup settings");
    return;
  }

  std::shared_ptr<CSettingGroup> groupRule = AddGroup(category);
  if (groupRule == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSRules: unable to setup settings");
    return;
  }

  std::shared_ptr<CSettingGroup> groupFilter = AddGroup(category);
  if (groupFilter == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSRules: unable to setup settings");
    return;
  }

  std::shared_ptr<CSettingGroup> groupExtra = AddGroup(category);
  if (groupFilter == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSRules: unable to setup settings");
    return;
  }

  std::shared_ptr<CSettingGroup> groupSave = AddGroup(category);
  if (groupFilter == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSRules: unable to setup settings");
    return;
  }

  // Save shader file in userdata folder
  g_dsSettings.pixelShaderList->SaveXML();

  if (m_ruleList.size() == 0)
  {
    // RULE
    m_dsmanager->InitConfig(m_ruleList, SPINNERATTR, "rules.priority", 60024, "priority", "",m_dsmanager->PriorityOptionFiller);
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "rules.name", 60002, "name"); 
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "rules.filetypes", 60003, "filetypes");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "rules.filename", 60004, "filename");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "rules.videocodec", 60020, "videocodec");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "rules.audiocodec", 60022, "audiocodec");
    m_dsmanager->InitConfig(m_ruleList, EDITATTR, "rules.protocols", 60005, "protocols");
    m_dsmanager->InitConfig(m_ruleList, BOOLATTR, "rules.url", 60006, "url");

    // FILTER
    m_dsmanager->InitConfig(m_ruleList, FILTER, "rules.source", 60007, "filter", "source", m_dsmanager->AllFiltersConfigOptionFiller);
    m_dsmanager->InitConfig(m_ruleList, FILTER, "rules.splitter", 60008, "filter", "splitter", m_dsmanager->AllFiltersConfigOptionFiller);
    m_dsmanager->InitConfig(m_ruleList, FILTER, "rules.video", 60009, "filter", "video", m_dsmanager->AllFiltersConfigOptionFiller);
    m_dsmanager->InitConfig(m_ruleList, FILTER, "rules.audio", 60010, "filter", "audio", m_dsmanager->AllFiltersConfigOptionFiller);
    m_dsmanager->InitConfig(m_ruleList, FILTER, "rules.subs", 60011, "filter", "subs", m_dsmanager->AllFiltersConfigOptionFiller);

    // EXTRAFILTER
    m_dsmanager->InitConfig(m_ruleList, EXTRAFILTER, "rules.extra0", 60012, "filter", "extra", m_dsmanager->AllFiltersConfigOptionFiller, 0, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.videores0", 60019, "videoresolution", "extra", 0, 0, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.videocodec0", 60020, "videocodec", "extra", 0, 0, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.audiochans0", 60021, "audiochannels", "extra", 0, 0, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.audiocodec0", 60022, "audiocodec", "extra", 0, 0, "extra");

    m_dsmanager->InitConfig(m_ruleList, EXTRAFILTER, "rules.extra1", 60012, "filter", "extra", m_dsmanager->AllFiltersConfigOptionFiller, 1, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.videores1", 60019, "videoresolution", "extra", 0, 1, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.videocodec1", 60020, "videocodec", "extra", 0, 1, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.audiochans1", 60021, "audiochannels", "extra", 0, 1, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.audiocodec1", 60022, "audiocodec", "extra", 0, 1, "extra");

    m_dsmanager->InitConfig(m_ruleList, EXTRAFILTER, "rules.extra2", 60012, "filter", "extra", m_dsmanager->AllFiltersConfigOptionFiller, 2, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.videores2", 60019, "videoresolution", "extra", 0, 2, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.videocodec2", 60020, "videocodec", "extra", 0, 2, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.audiochans2", 60021, "audiochannels", "extra", 0, 2, "extra");
    m_dsmanager->InitConfig(m_ruleList, EDITATTREXTRA, "rules.audiocodec2", 60022, "audiocodec", "extra", 0, 2, "extra");

    // SHADER
    m_dsmanager->InitConfig(m_ruleList, SHADER, "rules.shader0", 60013, "id", "shader", m_dsmanager->ShadersOptionFiller, 0, "shaders");
    m_dsmanager->InitConfig(m_ruleList, SPINNERATTRSHADER, "rules.shprepost0", 60023, "stage", "shader", m_dsmanager->ShadersScaleOptionFiller, 0, "shaders");
    m_dsmanager->InitConfig(m_ruleList, EDITATTRSHADER, "rules.shvideores0", 60019, "videoresolution", "shader", 0, 0, "shaders");
    m_dsmanager->InitConfig(m_ruleList, EDITATTRSHADER, "rules.shvideocodec0", 60020, "videocodec", "shader", 0, 0, "shaders");

    m_dsmanager->InitConfig(m_ruleList, SHADER, "rules.shader1", 60013, "id", "shader", m_dsmanager->ShadersOptionFiller, 1, "shaders");
    m_dsmanager->InitConfig(m_ruleList, SPINNERATTRSHADER, "rules.shprepost1", 60023, "stage", "shader", m_dsmanager->ShadersScaleOptionFiller, 1, "shaders");
    m_dsmanager->InitConfig(m_ruleList, EDITATTRSHADER, "rules.shvideores1", 60019, "videoresolution", "shader", 0, 1, "shaders");
    m_dsmanager->InitConfig(m_ruleList, EDITATTRSHADER, "rules.shvideocodec1", 60020, "videocodec", "shader", 0, 1, "shaders");

    m_dsmanager->InitConfig(m_ruleList, SHADER, "rules.shader2", 60013, "id", "shader", m_dsmanager->ShadersOptionFiller, 2, "shaders");
    m_dsmanager->InitConfig(m_ruleList, SPINNERATTRSHADER, "rules.shprepost2", 60023, "stage", "shader", m_dsmanager->ShadersScaleOptionFiller, 2, "shaders");
    m_dsmanager->InitConfig(m_ruleList, EDITATTRSHADER, "rules.shvideores2", 60019, "videoresolution", "shader", 0, 2, "shaders");
    m_dsmanager->InitConfig(m_ruleList, EDITATTRSHADER, "rules.shvideocodec2", 60020, "videocodec", "shader", 0, 2, "shaders");
  }

  // Reset Button value
  m_dsmanager->ResetValue(m_ruleList);

  // Load userdata Mediaseconfig.xml
  if (!(m_dsmanager->GetNew()))
  {
    TiXmlElement *pRules;
    m_dsmanager->LoadDsXML(MEDIASCONFIG, pRules);

    if (pRules)
    {
      TiXmlElement *pRule = m_dsmanager->KeepSelectedNode(pRules, "rule");

      for (const auto &it : m_ruleList)
      {
        if (it->m_configType == EDITATTR || it->m_configType == SPINNERATTR)
          it->m_value = CDSXMLUtils::GetString(pRule, it->m_attr.c_str());

        if (it->m_configType == BOOLATTR)
        {
          it->m_value = CDSXMLUtils::GetString(pRule, it->m_attr.c_str());
          if (it->m_value == "")
            it->m_value = "false";
        }

        if (it->m_configType == FILTER)
        {
          TiXmlElement *pFilter = pRule->FirstChildElement(it->m_nodeName.c_str());
          if (pFilter)
            it->m_value = CDSXMLUtils::GetString(pFilter, it->m_attr.c_str());
        }

        if (it->m_configType == EXTRAFILTER
          || it->m_configType == SHADER
          || it->m_configType == EDITATTREXTRA
          || it->m_configType == SPINNERATTRSHADER
          || it->m_configType == EDITATTRSHADER)
        {
          TiXmlElement *pFilter;

          pFilter = pRule->FirstChildElement(it->m_nodeName.c_str());

          if (pFilter && NodeHasAttr(pFilter, it->m_attr))
          {
            if (it->m_subNode == 0)
              it->m_value = CDSXMLUtils::GetString(pFilter, it->m_attr.c_str());

            continue;
          }

          pFilter = pRule->FirstChildElement(it->m_nodeList.c_str());
          if (pFilter)
          {
            int countsize = 0;
            TiXmlElement *pSubExtra = pFilter->FirstChildElement(it->m_nodeName.c_str());
            while (pSubExtra)
            {
              if (it->m_subNode == countsize)
                it->m_value = CDSXMLUtils::GetString(pSubExtra, it->m_attr.c_str());

              pSubExtra = pSubExtra->NextSiblingElement(it->m_nodeName.c_str());
              countsize++;
            }
          }
        }
      }
    }
  }

  // Stamp Button
  std::shared_ptr<CSettingGroup> groupTmp;

  for (const auto &it : m_ruleList)
  {
    if (it->m_configType == EDITATTR
      || it->m_configType == EDITATTREXTRA
      || it->m_configType == EDITATTRSHADER)
    {
      if (it->m_configType == EDITATTREXTRA || it->m_configType == EDITATTRSHADER)
        groupTmp = groupExtra;
      else
        groupTmp = groupRule;

      AddEdit(groupTmp, it->m_setting, it->m_label, SettingLevel::Basic, it->m_value.c_str(), true);
    }
    if (it->m_configType == SPINNERATTR)
      AddList(groupPriority, it->m_setting, it->m_label,SettingLevel::Basic , it->m_value, it->m_filler, it->m_label);

    if (it->m_configType == SPINNERATTRSHADER)
      AddSpinner(groupExtra, it->m_setting, it->m_label,SettingLevel::Basic , it->m_value, it->m_filler);

    if (it->m_configType == BOOLATTR)
      AddToggle(groupRule, it->m_setting, it->m_label,SettingLevel::Basic , it->GetBoolValue());

    if (it->m_configType == FILTER)
      AddList(groupFilter, it->m_setting, it->m_label,SettingLevel::Basic , it->m_value, it->m_filler, it->m_label);

    if (it->m_configType == EXTRAFILTER || it->m_configType == SHADER)
      AddList(groupExtra, it->m_setting, it->m_label,SettingLevel::Basic , it->m_value, it->m_filler, it->m_label);
  }


  if (!m_dsmanager->GetNew())
    AddButton(groupSave, SETTING_RULE_DEL, 60017, SettingLevel::Basic);
}

void CGUIDialogDSRules::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
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
  HideUnused();
}

void CGUIDialogDSRules::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;  
  
  // Init variables
  CGUIDialogSettingsManualBase::OnSettingAction(setting);
  const std::string &settingId = setting->GetId();

  ActionInternal(settingId);
}

void CGUIDialogDSRules::ActionInternal(const std::string &settingId)
{
  if (settingId == "")
    return;

  // Load userdata Mediaseconfig.xml
  TiXmlElement *pRules;
  m_dsmanager->LoadDsXML(MEDIASCONFIG, pRules, true);
  if (!pRules)
    return;

  // Del Rule
  if (settingId == SETTING_RULE_DEL)
  {

    if (!CGUIDialogYesNo::ShowAndGetInput(60017, 60018, 0, 0))
      return;

    TiXmlElement *oldRule = m_dsmanager->KeepSelectedNode(pRules, "rule");
    pRules->RemoveChild(oldRule);

    m_dsmanager->SaveDsXML(MEDIASCONFIG);
    CGUIDialogDSRules::Close();
  }

  // Add & Save Rule
  if (settingId == SETTING_RULE_SAVE || settingId == SETTING_RULE_ADD)
  {

    bool isMadvr = (StringUtils::ToLower(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_DSPLAYER_VIDEORENDERER)) == "madvr");
    TiXmlElement pRule("rule");

    for (const auto &it : m_ruleList)
    {
      if (it->m_configType == EDITATTR && it->m_value != "")
        pRule.SetAttribute(it->m_attr.c_str(), it->m_value.c_str());

      if (it->m_configType == SPINNERATTR && it->m_value != "[null]")
        pRule.SetAttribute(it->m_attr.c_str(), it->m_value.c_str());

      if (it->m_configType == BOOLATTR && it->m_value != "false")
        pRule.SetAttribute(it->m_attr.c_str(), it->m_value.c_str());

      if (it->m_configType == FILTER && it->m_value != "[null]")
      {
        pRule.InsertEndChild(TiXmlElement(it->m_nodeName.c_str()));
        TiXmlElement *pFilter = pRule.FirstChildElement(it->m_nodeName.c_str());
        if (pFilter)
          pFilter->SetAttribute(it->m_attr.c_str(), it->m_value.c_str());
      }

      if ((it->m_configType == EXTRAFILTER
        || it->m_configType == SHADER
        || it->m_configType == EDITATTREXTRA
        || it->m_configType == EDITATTRSHADER
        || it->m_configType == SPINNERATTRSHADER)
        && it->m_value != "[null]" && it->m_value != "")
      {

        if (!isMadvr && it->m_configType == SPINNERATTRSHADER)
          continue;

        TiXmlElement *pExtra = pRule.FirstChildElement(it->m_nodeList.c_str());
        if (!pExtra && it->m_configType != EDITATTREXTRA && it->m_configType != EDITATTRSHADER && it->m_configType != SPINNERATTRSHADER)
        {
          pRule.InsertEndChild(TiXmlElement(it->m_nodeList.c_str()));
          pExtra = pRule.FirstChildElement(it->m_nodeList.c_str());
        }
        if (it->m_configType != EDITATTREXTRA && it->m_configType != EDITATTRSHADER && it->m_configType != SPINNERATTRSHADER)
          pExtra->InsertEndChild(TiXmlElement(it->m_nodeName.c_str()));

        if (!pExtra)
          continue;

        TiXmlElement *pSubExtra = pExtra->FirstChildElement(it->m_nodeName.c_str());

        int countsize = 0;
        while (pSubExtra)
        {
          if (it->m_subNode == countsize)
            pSubExtra->SetAttribute(it->m_attr.c_str(), it->m_value.c_str());

          pSubExtra = pSubExtra->NextSiblingElement(it->m_nodeName.c_str());
          countsize++;
        }
      }
    }

    // SAVE
    if (settingId == SETTING_RULE_SAVE)
    {
      TiXmlElement *oldRule = m_dsmanager->KeepSelectedNode(pRules, "rule");
      pRules->ReplaceChild(oldRule, pRule);
    }

    if (settingId == SETTING_RULE_ADD)
      pRules->InsertEndChild(pRule);

    m_bEdited = false;
    m_dsmanager->SaveDsXML(MEDIASCONFIG);
    CGUIDialogDSRules::Close();
  }
}

void CGUIDialogDSRules::ShowDSRulesList()
{
  // Load userdata Mediaseconfig.xml
  TiXmlElement *pRules;
  CGUIDialogDSManager::Get()->LoadDsXML(MEDIASCONFIG, pRules, true);
  if (!pRules)
    return;

  std::string strRule;
  int selectedId = -1;
  int selected = -1;
  int count = 0;
  int id = 0;

  CGUIDialogSelect *pDlg = (CGUIDialogSelect *)CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_SELECT);
  if (!pDlg)
    return;

  pDlg->SetHeading(60001);

  TiXmlElement *pRule = pRules->FirstChildElement("rule");
  std::vector<CRules *> rules;
  while (pRule)
  {
    CRules *rule = new CRules();

    rule->strName = CDSXMLUtils::GetString(pRule, "name");
    rule->strfileTypes = CDSXMLUtils::GetString(pRule, "filetypes");
    rule->strfileName = CDSXMLUtils::GetString(pRule, "filename");
    rule->strVideoCodec = CDSXMLUtils::GetString(pRule, "videocodec");
    rule->strAudioCodec = CDSXMLUtils::GetString(pRule, "audiocodec");
    rule->strProtocols = CDSXMLUtils::GetString(pRule, "protocols");
    if (!CDSXMLUtils::GetString(pRule, "priority", &rule->strPriority))
      rule->strPriority = "0";

    rule->id = id;
    rules.emplace_back(std::move(rule));

    pRule = pRule->NextSiblingElement("rule");
    id++;
  }
  std::sort(rules.begin(), rules.end(), compare_by_word);
  
  for (unsigned int i = 0; i < rules.size(); i++)
  {

    if (rules[i]->strfileTypes != "")
      rules[i]->strfileTypes = StringUtils::Format("Filetypes={}", rules[i]->strfileTypes.c_str());
    if (rules[i]->strfileName != "")
      rules[i]->strfileName = StringUtils::Format("Filename={}", rules[i]->strfileName.c_str());
    if (rules[i]->strVideoCodec != "")
      rules[i]->strVideoCodec = StringUtils::Format("Videocodec={}", rules[i]->strVideoCodec.c_str());
    if (rules[i]->strVideoCodec != "")
      rules[i]->strVideoCodec = StringUtils::Format("Audiocodec={}", rules[i]->strAudioCodec.c_str());
    if (rules[i]->strProtocols != "")
      rules[i]->strProtocols = StringUtils::Format("Protocols={}", rules[i]->strProtocols.c_str());

    if (rules[i]->strName != "")
      strRule = rules[i]->strName;
    else
    {
      strRule = StringUtils::Format("{} {} {} {} {}", rules[i]->strfileTypes.c_str(), rules[i]->strfileName.c_str(), rules[i]->strVideoCodec.c_str(), rules[i]->strAudioCodec.c_str(), rules[i]->strProtocols.c_str());
      StringUtils::Trim(strRule);
    }

    if (rules[i]->strPriority != "")
    {
      strRule = StringUtils::Format("{} ({})", strRule.c_str(), rules[i]->strPriority.c_str());
    }

    strRule = StringUtils::Format("Rule:   {}", strRule.c_str());
    pDlg->Add(strRule);
    count++;
  }

  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(60014).c_str());
  pDlg->Open();

  selected = pDlg->GetSelectedItem();
  if (selected > -1 && selected < rules.size()) 
    selectedId = rules[selected]->id;

  CGUIDialogDSManager::Get()->SetConfig(selected == count, selectedId);

  if (selected > -1)
    CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_DIALOG_DSRULES);
}


