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

#include "GUIDialogDSManager.h"
#include "cores/DSPlayer/Utils/DSFilterEnumerator.h"
#include "profiles/ProfileManager.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/log.h"
#include "utils/DSFileUtils.h"
#include <iterator>
#include "utils/StringUtils.h"
#include "ServiceBroker.h"
using namespace std;

DSConfigList::DSConfigList(ConfigType type) :

m_setting(""),
m_nodeName(""),
m_nodeList(""),
m_attr(""),
m_value(""),
m_label(0),
m_subNode(0),
m_filler(NULL),
m_configType(type)
{
}

CGUIDialogDSManager::CGUIDialogDSManager()
{
}

CGUIDialogDSManager::~CGUIDialogDSManager()
{
}

CGUIDialogDSManager *CGUIDialogDSManager::m_pSingleton = NULL;

CGUIDialogDSManager* CGUIDialogDSManager::Get()
{
  return (m_pSingleton) ? m_pSingleton : (m_pSingleton = new CGUIDialogDSManager());
}

void CGUIDialogDSManager::InitConfig(std::vector<DSConfigList *> &configList, ConfigType type, const std::string & strSetting, int label, const std::string & strAttr /* = "" */, const std::string & strNodeName /*= "" */, StringSettingOptionsFiller filler /* = NULL */, int subNode /* = 0 */, const std::string & strNodeList /* = "" */)
{
  DSConfigList* list = new DSConfigList(type);

  list->m_setting = strSetting;
  list->m_attr = strAttr;
  list->m_nodeName = strNodeName;
  list->m_nodeList = strNodeList;
  list->m_filler = filler;
  list->m_label = label;
  list->m_subNode = subNode;
  configList.emplace_back(std::move(list));
}

void CGUIDialogDSManager::ResetValue(std::vector<DSConfigList *> &configList)
{
  for (const auto &it : configList)
  {
    if (it->m_configType == EDITATTR
      || it->m_configType == EDITATTREXTRA
      || it->m_configType == EDITATTRSHADER
      || it->m_configType == OSDGUID)
      it->m_value = "";

    if (it->m_configType == SPINNERATTR
      || it->m_configType == FILTER
      || it->m_configType == EXTRAFILTER
      || it->m_configType == SHADER
      || it->m_configType == FILTERSYSTEM)
      it->m_value = "[null]";

    if (it->m_configType == SPINNERATTRSHADER)
      it->m_value = "preresize";

    if (it->m_configType == BOOLATTR)
      it->m_value = "false";
  }
}

void CGUIDialogDSManager::GetPath(xmlType type, std::string &xmlFile, std::string &xmlNode, std::string &xmlRoot)
{
  if (type == MEDIASCONFIG)
  {
    
    xmlFile = CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/mediasconfig.xml");
    xmlRoot = "mediasconfig";
    xmlNode = "rules";
  }

  if (type == FILTERSCONFIG)
  {
    xmlFile = CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/filtersconfig.xml");
    xmlRoot = "filtersconfig";
    xmlNode = "filters";
  }

  if (type == HOMEFILTERSCONFIG)
  {
    xmlFile = "special://xbmc/system/players/dsplayer/filtersconfig.xml";
    xmlRoot = "filtersconfig";
    xmlNode = "filters";
  }

  if (type == SHADERS)
  {
    xmlFile = CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/shaders.xml");
    xmlRoot = "shaders";
  }

  if (type == PLAYERCOREFACTORY)
  {
    xmlFile = CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("playercorefactory.xml");
    xmlRoot = "playercorefactory";
    xmlNode = "rules";
  }
}

void CGUIDialogDSManager::SaveDsXML(xmlType type)
{
  std::string xmlFile, xmlNode, xmlRoot;
  GetPath(type, xmlFile, xmlNode, xmlRoot);

  m_XML.SaveFile(xmlFile);
}

bool CGUIDialogDSManager::FindPrepend(TiXmlElement* &pNode, const std::string &xmlNode)
{
  bool isPrepend = false;
  while (pNode)
  {
    std::string value;
    value = CDSXMLUtils::GetString(pNode, "action");
    if (value == "prepend")
    {
      isPrepend = true;
      break;
    }
    pNode = pNode->NextSiblingElement(xmlNode.c_str());
  }
  return isPrepend;
}

void CGUIDialogDSManager::LoadDsXML(xmlType type, TiXmlElement* &pNode, bool forceCreate /*= false*/)
{
  std::string xmlFile, xmlNode, xmlRoot;
  GetPath(type, xmlFile, xmlNode, xmlRoot);

  pNode = NULL;

  m_XML.Clear();

  if (!m_XML.LoadFile(xmlFile))
  {
    CLog::Log(LOGERROR, "{} Error loading {}, Line %d ({})", __FUNCTION__, xmlFile.c_str(), m_XML.ErrorRow(), m_XML.ErrorDesc());
    if (!forceCreate)
      return;

    CLog::Log(LOGDEBUG, "{} Creating loading {}, with root <{}> and first node <{}>", __FUNCTION__, xmlFile.c_str(), xmlRoot.c_str(), xmlNode.c_str());

    TiXmlElement pRoot(xmlRoot.c_str());
    if (type != PLAYERCOREFACTORY)
      pRoot.InsertEndChild(TiXmlElement(xmlNode.c_str()));
    m_XML.InsertEndChild(pRoot);
  }
  TiXmlElement *pConfig = m_XML.RootElement();
  if (!pConfig || strcmpi(pConfig->Value(), xmlRoot.c_str()) != 0)
  {
    CLog::Log(LOGERROR, "{} Error loading medias configuration, no <{}> node", __FUNCTION__, xmlRoot.c_str());
    return;
  }
  if (type == MEDIASCONFIG
    || type == FILTERSCONFIG
    || type == HOMEFILTERSCONFIG)
    pNode = pConfig->FirstChildElement(xmlNode.c_str());

  if (type == SHADERS)
    pNode = pConfig;

  if (type == PLAYERCOREFACTORY) {

    pNode = pConfig->FirstChildElement(xmlNode.c_str());

    if (!FindPrepend(pNode, xmlNode))
    {
      TiXmlElement pTmp(xmlNode.c_str());
      pTmp.SetAttribute("action", "prepend");
      pConfig->InsertEndChild(pTmp);

      pNode = pConfig->FirstChildElement(xmlNode.c_str());
      FindPrepend(pNode, xmlNode);
    }
  }
}

void CGUIDialogDSManager::GetFilterList(xmlType type, std::vector<StringSettingOption> &list)
{
  list.emplace_back("", "[null]");

  // Load filtersconfig.xml
  TiXmlElement *pFilters;
  LoadDsXML(type, pFilters);

  std::string strFilter;
  std::string strFilterLabel;

  if (pFilters)
  {

    TiXmlElement *pFilter = pFilters->FirstChildElement("filter");
    while (pFilter)
    {
      TiXmlElement *pOsdname = pFilter->FirstChildElement("osdname");
      if (pOsdname)
      {
        XMLUtils::GetString(pFilter, "osdname", strFilterLabel);
        strFilter = CDSXMLUtils::GetString(pFilter, "name");
        strFilterLabel = StringUtils::Format("{} ({})", strFilterLabel.c_str(), strFilter.c_str());

        list.emplace_back(strFilterLabel, strFilter);
      }
      pFilter = pFilter->NextSiblingElement("filter");
    }
  }
}

template <typename T>
const bool Contains(std::vector<T>& Vec, const T& Element)
{
  if (std::find(Vec.begin(), Vec.end(), Element) != Vec.end())
    return true;

  return false;
}

void CGUIDialogDSManager::AllFiltersConfigOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current, void* data)
{
  std::vector<StringSettingOption> listUserdata;
  std::vector<StringSettingOption> listHome;
  Get()->GetFilterList(FILTERSCONFIG, listUserdata);
  Get()->GetFilterList(HOMEFILTERSCONFIG, listHome);

  //std::sort(listUserdata.begin(), listUserdata.end());
  //std::sort(listHome.begin(), listHome.end());

  list.reserve(listUserdata.size() + listHome.size());
  for (std::vector<StringSettingOption>::iterator it = listUserdata.begin(); it != listUserdata.end(); it++)
    list.push_back(*it);

  for (std::vector<StringSettingOption>::iterator it = listHome.begin(); it != listHome.end(); it++)
  {
    if (Contains(listUserdata,*it))
      CLog::Log(LOGDEBUG, "already found {}", __FUNCTION__);
    else
      list.push_back(*it);
    
  }

  //std::merge(listUserdata.begin(), listUserdata.end(), listHome.begin(), listHome.end(), std::back_inserter(list));

  std::sort(list.begin(), list.end(), compare_by_word);
  //list.erase(unique(list.begin(), list.end()), list.end());

}

void CGUIDialogDSManager::ShadersOptionFiller(const std::shared_ptr<const CSetting>& setting,
  StringSettingOptions& list,
  std::string& current,
  void* data)
{
  list.emplace_back("", "[null]");

  TiXmlElement *pShaders;

  // Load userdata shaders.xml
  Get()->LoadDsXML(SHADERS, pShaders);

  std::string strShader;
  std::string strShaderLabel;

  if (!pShaders)
    return;

  TiXmlElement *pShader = pShaders->FirstChildElement("shader");
  while (pShader)
  {
    strShaderLabel = CDSXMLUtils::GetString(pShader, "name");
    strShader = CDSXMLUtils::GetString(pShader, "id");
    strShaderLabel = StringUtils::Format("{} ({})", strShaderLabel.c_str(), strShader.c_str());

    list.emplace_back(strShaderLabel, strShader);

    pShader = pShader->NextSiblingElement("shader");
  }
}

void CGUIDialogDSManager::ShadersScaleOptionFiller(const std::shared_ptr<const CSetting>& setting,
  StringSettingOptions& list,
  std::string& current,
  void* data)
{
  list.emplace_back("Pre-resize", "preresize");
  list.emplace_back("Post-resize", "postresize");
}

void CGUIDialogDSManager::DSFilterOptionFiller(const std::shared_ptr<const CSetting>& setting, StringSettingOptions& list, std::string& current, void* data)
{
  CDSFilterEnumerator p_dfilter;
  std::vector<DSFiltersInfo> filterList;
  p_dfilter.GetDSFilters(filterList);

  list.emplace_back("", "[null]");

  for (const auto &it : filterList)
    list.emplace_back(it.lpstrName, it.lpstrGuid);
}

void CGUIDialogDSManager::BoolOptionFiller(std::shared_ptr<const CSetting>& setting, std::vector< std::pair<std::string, std::string> > &list, std::string &current, void *data)
{
  list.emplace_back("[null]", "[null]");
  list.emplace_back("true", "true");
  list.emplace_back("false", "false");
}

void CGUIDialogDSManager::PriorityOptionFiller(const std::shared_ptr<const CSetting>& setting,
  StringSettingOptions& list,
  std::string& current,
  void* data)
{
  list.emplace_back("", "[null]");

  for (unsigned int i = 0; i < 10; i++)
  {
    std::string sValue;
    sValue = StringUtils::Format("%i",i);
    list.emplace_back(sValue, sValue);
  }
}

TiXmlElement* CGUIDialogDSManager::KeepSelectedNode(TiXmlElement* pNode, const std::string &subNodeName)
{
  int count = 0;

  TiXmlElement *pRule = pNode->FirstChildElement(subNodeName.c_str());
  while (pRule)
  {
    if (count == m_iIndex)
      break;

    pRule = pRule->NextSiblingElement(subNodeName.c_str());
    count++;
  }
  return pRule;
}

bool CGUIDialogDSManager::compare_by_word(const StringSettingOption& lhs, const StringSettingOption& rhs)
{
  std::string strLine1 = lhs.label;
  std::string strLine2 = rhs.label;
  StringUtils::ToLower(strLine1);
  StringUtils::ToLower(strLine2);
  return strcmp(strLine1.c_str(), strLine2.c_str()) < 0;
}


