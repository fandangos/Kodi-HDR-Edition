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

#include "GUIDialogDSFilters.h"
#include "application/Application.h"
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
#include "cores/DSPlayer/Utils/DSFilterEnumerator.h"
#include "utils/XMLUtils.h"
#include "Filters/RendererSettings.h"
#include "PixelShaderList.h"
#include "utils/DSFileUtils.h"
#include "ServiceBroker.h"
#include "guilib/GUIComponent.h"
#include "resources/ResourcesComponent.h"

#define SETTING_FILTER_SAVE                   "dsfilters.save"
#define SETTING_FILTER_ADD                    "dsfilters.add"
#define SETTING_FILTER_DEL                    "dsfilters.del"

using namespace std;

CGUIDialogDSFilters::CGUIDialogDSFilters()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_DSFILTERS, "DialogSettings.xml")
{
  m_dsmanager = CGUIDialogDSManager::Get();
}

CGUIDialogDSFilters::~CGUIDialogDSFilters()
{ 
}

void CGUIDialogDSFilters::OnInitWindow()
{
  CGUIDialogSettingsManualBase::OnInitWindow();
  m_bEdited = false;
}

void CGUIDialogDSFilters::OnDeinitWindow(int nextWindowID)
{
  if (m_bEdited)
    m_dsmanager->GetNew() ? ActionInternal(SETTING_FILTER_ADD) : ActionInternal(SETTING_FILTER_SAVE);

  CGUIDialogSettingsManualBase::OnDeinitWindow(nextWindowID);
  ShowDSFiltersList();
}

bool CGUIDialogDSFilters::Save()
{
  return false;
}

void CGUIDialogDSFilters::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(65001);

  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogDSFilters::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  const std::shared_ptr<CSettingCategory> category = AddCategory("dsfiltersettings", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSFilters: unable to setup settings");
    return;
  }

  // get all necessary setting groups
  const std::shared_ptr<CSettingGroup> groupSystem = AddGroup(category);
  if (groupSystem == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSFilters: unable to setup settings");
    return;
  }

  const std::shared_ptr<CSettingGroup> group = AddGroup(category);
  if (group == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSFilters: unable to setup settings");
    return;
  }

  const std::shared_ptr<CSettingGroup> groupSave = AddGroup(category);
  if (groupSave == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogDSFilters: unable to setup settings");
    return;
  }

  // Init variables
  std::string strGuid;

  if (m_filterList.size() == 0)
  {
    m_dsmanager->InitConfig(m_filterList, OSDGUID, "dsfilters.osdname", 65003, "", "osdname");
    m_dsmanager->InitConfig(m_filterList, EDITATTR, "dsfilters.name", 65004, "name");
    m_dsmanager->InitConfig(m_filterList, FILTER, "dsfilters.type", 65005, "type", "", TypeOptionFiller);
    m_dsmanager->InitConfig(m_filterList, OSDGUID, "dsfilters.guid", 65006, "", "guid");
    m_dsmanager->InitConfig(m_filterList, FILTERSYSTEM, "dsfilters.systemfilter", 65010, "", "", m_dsmanager->DSFilterOptionFiller);

  }

  // Reset Button value
  m_dsmanager->ResetValue(m_filterList);

  // Load userdata Filteseconfig.xml
  if (!m_dsmanager->GetNew())
  {
    TiXmlElement *pFilters;
    m_dsmanager->LoadDsXML(FILTERSCONFIG, pFilters);

    if (pFilters)
    {
      TiXmlElement *pFilter = m_dsmanager->KeepSelectedNode(pFilters, "filter");

      for (const auto &it : m_filterList)
      {
        if (it->m_configType == EDITATTR || it->m_configType == FILTER)
          it->m_value = CDSXMLUtils::GetString(pFilter, it->m_attr.c_str());

        if (it->m_configType == OSDGUID) {
          XMLUtils::GetString(pFilter, it->m_nodeName.c_str(), strGuid);
          it->m_value = strGuid;
        }
        if (it->m_configType == FILTERSYSTEM)
          it->m_value = strGuid;
      }
    }
  }

  // Stamp Button
  for (const auto &it : m_filterList)
  {
    if (it->m_configType == FILTERSYSTEM)
      AddList(groupSystem, it->m_setting, it->m_label, SettingLevel::Basic, it->m_value, it->m_filler, it->m_label);

    if (it->m_configType == EDITATTR || it->m_configType == OSDGUID)
      AddEdit(group, it->m_setting, it->m_label, SettingLevel::Basic, it->m_value, true);

    if (it->m_configType == FILTER)
      AddList(group, it->m_setting, it->m_label, SettingLevel::Basic, it->m_value, it->m_filler, it->m_label);
  }

  if (!m_dsmanager->GetNew())
    AddButton(groupSave, SETTING_FILTER_DEL, 65009, SettingLevel::Basic);
}

void CGUIDialogDSFilters::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  m_bEdited = true;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);
  const std::string &settingId = setting->GetId();

  for (const auto &it : m_filterList)
  {
    if (it->m_configType == EDITATTR
      || it->m_configType == FILTER
      || it->m_configType == OSDGUID)
    {
      if (settingId == it->m_setting)
      {
        it->m_value = std::static_pointer_cast<const CSettingString>(setting)->GetValue();
      }
    }
    if (it->m_configType == FILTERSYSTEM)
    {
      if (settingId == "dsfilters.systemfilter")
      {
        it->m_value = std::static_pointer_cast<const CSettingString>(setting)->GetValue();

        if (it->m_value != "[null]")
        {
          std::string strOSDName = GetFilterName(it->m_value);
          std::string strFilterName = strOSDName;
          StringUtils::ToLower(strFilterName);
          StringUtils::Replace(strFilterName, " ", "_");
          GetSettingsManager()->SetString("dsfilters.guid", it->m_value.c_str());
          GetSettingsManager()->SetString("dsfilters.osdname", strOSDName);
          GetSettingsManager()->SetString("dsfilters.name", strFilterName);
        }
      }
    }
  }
}

void CGUIDialogDSFilters::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  // Init variables
  CGUIDialogSettingsManualBase::OnSettingAction(setting);
  const std::string &settingId = setting->GetId();

  ActionInternal(settingId);
}

void CGUIDialogDSFilters::ActionInternal(const std::string &settingId)
{
  if (settingId == "")
    return;

  // Load userdata Filteseconfig.xml
  TiXmlElement *pFilters;
  m_dsmanager->LoadDsXML(FILTERSCONFIG, pFilters, true);
  if (!pFilters)
    return;

  // Del Filter
  if (settingId == SETTING_FILTER_DEL)
  {
    if (!CGUIDialogYesNo::ShowAndGetInput(65009, 65011, 0, 0))
      return;

    TiXmlElement *oldRule = m_dsmanager->KeepSelectedNode(pFilters, "filter");
    pFilters->RemoveChild(oldRule);

    m_dsmanager->SaveDsXML(FILTERSCONFIG);
    CGUIDialogDSFilters::Close();
  }

  // Add & Save Filter
  if (settingId == SETTING_FILTER_ADD || settingId == SETTING_FILTER_SAVE)
  {
    TiXmlElement pFilter("filter");

    for (const auto &it : m_filterList)
    {
      if (it->m_value == "" || it->m_value == "[null]")
      {
        CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(65001), CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(65012), 2000, false, 300);
        return;
      }

      if (it->m_configType == EDITATTR || it->m_configType == FILTER)
        pFilter.SetAttribute(it->m_attr.c_str(), it->m_value.c_str());

      if (it->m_configType == OSDGUID)
      {
        TiXmlElement newElement(it->m_nodeName.c_str());
        TiXmlNode *pNewNode = pFilter.InsertEndChild(newElement);
        TiXmlText value(it->m_value.c_str());
        pNewNode->InsertEndChild(value);
      }
    }

    // SAVE
    if (settingId == SETTING_FILTER_SAVE)
    {
      TiXmlElement *oldFilter = m_dsmanager->KeepSelectedNode(pFilters, "filter");
      pFilters->ReplaceChild(oldFilter, pFilter);
    }

    if (settingId == SETTING_FILTER_ADD)
      pFilters->InsertEndChild(pFilter);

    m_bEdited = false;
    m_dsmanager->SaveDsXML(FILTERSCONFIG);
    CGUIDialogDSFilters::Close();
  }
}

void CGUIDialogDSFilters::ShowDSFiltersList()
{
  // Load userdata Filterseconfig.xml
  TiXmlElement *pFilters;
  CGUIDialogDSManager::Get()->LoadDsXML(FILTERSCONFIG, pFilters, true);
  if (!pFilters)
    return;

  int selected;
  int count = 0;
  CGUIDialogSelect* pDlg = (CGUIDialogSelect*)CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_SELECT);

  if (!pDlg)
    return;

  pDlg->SetHeading(65001);

  std::string strFilter;
  std::string strFilterLabel;

  TiXmlElement *pFilter = pFilters->FirstChildElement("filter");
  while (pFilter)
  {
    strFilterLabel = "";
    strFilter = "";

    TiXmlElement *pOsdname = pFilter->FirstChildElement("osdname");
    if (pOsdname)
      XMLUtils::GetString(pFilter, "osdname", strFilterLabel);

    strFilter = CDSXMLUtils::GetString(pFilter, "name");
    strFilterLabel = StringUtils::Format("{} ({})", strFilterLabel.c_str(), strFilter.c_str());
    pDlg->Add(strFilterLabel.c_str()); 
    count++;

    pFilter = pFilter->NextSiblingElement("filter");
  }

  pDlg->Add(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(65002).c_str());

  pDlg->Open();
  selected = pDlg->GetSelectedItem();

  CGUIDialogDSManager::Get()->SetConfig(selected == count, selected);

  if (selected > -1) 
    CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_DIALOG_DSFILTERS);
}

void CGUIDialogDSFilters::TypeOptionFiller(const std::shared_ptr<const CSetting>& setting, StringSettingOptions& list, std::string& current, void* data)
{
  list.emplace_back("", "[null]");
  list.emplace_back("Source Filter (source)", "source");
  list.emplace_back("Splitter Filter (splitter)", "splitter");
  list.emplace_back("Video Decoder (videodec)", "videodec");
  list.emplace_back("Audio Decoder (audiodec)", "audiodec");
  list.emplace_back("Subtitles Filter (subs)", "subs");
  list.emplace_back("Extra Filter (extra)", "extra");
}

std::string CGUIDialogDSFilters::GetFilterName(std::string guid)
{
  CDSFilterEnumerator p_dfilter;
  std::vector<DSFiltersInfo> filterList;
  p_dfilter.GetDSFilters(filterList);

  for (const auto &it : filterList)
  {
    if (guid == it.lpstrGuid)
      return it.lpstrName;
  }
  return "";
}


