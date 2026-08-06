/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
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
// MadvrSettings.cpp: implementation of the CMadvrSettings class.
//
//////////////////////////////////////////////////////////////////////

#include "MadvrSettings.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "Utils/Log.h"
#include "cores/DSPlayer/Dialogs/GUIDialogDSManager.h"
#include "utils/StringUtils.h"
#include "utils/RegExp.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "profiles/ProfileManager.h"
#include "DSFilterVersion.h"
#include "FileItem.h"
#include "Application/Application.h"
#include "utils/DSFileUtils.h"
#include "settings/SettingsComponent.h"
#include <resources/LocalizeStrings.h>
#include "ServiceBroker.h"
#include "resources/LocalizeStrings.h"
#include "resources/ResourcesComponent.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

#ifndef countof
#define countof(array) (sizeof(array)/sizeof(array[0]))
#endif

CMadvrSettings::CMadvrSettings()
{
  m_Resolution = -1;
  m_TvShowName = "";
  m_FileStringPo = "";
  m_madvrJsonAtStart = "";
  m_iSubSectionId = 0;
  m_bDebug = false;
  m_bInitied = false;
  //InitSettings();


}

void CMadvrSettings::LateInit()
{
  if (!m_bInitied)
    InitSettings();
}
void CMadvrSettings::UpdateSettings()
{
  m_Resolution = -1;
  m_TvShowName = "";
  m_FileStringPo = "";
  m_madvrJsonAtStart = "";
  m_iSubSectionId = 0;
  m_bDebug = false;
  m_db.clear();
  m_dbDefault.clear();
  m_gui.clear();
  m_sections.clear();
  m_profiles.clear();
  g_application.LoadLanguage(true);
  
  InitSettings();
}

void CMadvrSettings::InitSettings()
{
  // Capture Filters Version
  CDSFilterVersion::Get()->InitVersion();

  // Init Variables
  std::string sVersionSuffix;
  std::string sMadvrSettingsXML;

  // Check For madvrsettings.xml

  // UserData DSPlayer
  
  if ( XFILE::CFile::Exists(CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/madvrsettings.xml"))
    && XFILE::CFile::Exists(CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/strings.po")) )
  { 
    sMadvrSettingsXML = CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/madvrsettings.xml");
    m_FileStringPo = CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetUserDataItem("dsplayer/strings.po");
    CLog::Log(LOGINFO, "{} loading madvrSettings.xml from appdata/kodi/userdata/dsplayer", __FUNCTION__);
  } 
  // Appdata addons/script.madvrsettings
  else if (XFILE::CDirectory::Exists("special://home/addons/script.madvrsettings/")) 
  {
    sVersionSuffix = GetVersionSuffix("special://home/addons/script.madvrsettings/resources/");
    sMadvrSettingsXML = StringUtils::Format("special://home/addons/script.madvrsettings/resources/{}_madvrsettings.xml", sVersionSuffix.c_str());
    m_FileStringPo = StringUtils::Format("special://home/addons/script.madvrsettings/resources/{}_strings.po", sVersionSuffix.c_str());
    CLog::Log(LOGINFO, "{} loading {}_madvrSettings.xml from appdata/kodi/addons/script.madvrsettings", __FUNCTION__, sVersionSuffix.c_str());
  }
  // InstallDir addons/script.madvrsettings
  else if (XFILE::CDirectory::Exists("special://xbmc/addons/script.madvrsettings/"))
  {
    sVersionSuffix = GetVersionSuffix("special://xbmc/addons/script.madvrsettings/resources/");
    sMadvrSettingsXML = StringUtils::Format("special://xbmc/addons/script.madvrsettings/resources/{}_madvrsettings.xml", sVersionSuffix.c_str());
    m_FileStringPo = StringUtils::Format("special://xbmc/addons/script.madvrsettings/resources/{}_strings.po", sVersionSuffix.c_str());
    CLog::Log(LOGINFO, "{} loading {}_madvrSettings.xml from kodi/addons/script.madvrsettings", __FUNCTION__, sVersionSuffix.c_str());
  }

  // Load settings strutcture for madVR
  TiXmlElement *pSettings;
  LoadMadvrXML(sMadvrSettingsXML, "settings", pSettings);
  if (pSettings)
  {
    AddProfiles(pSettings);
    AddSection(pSettings, MADVR_VIDEO_ROOT);
  }

  m_dbDefault = m_db;
  m_bInitied = true;
}

void CMadvrSettings::LoadMadvrXML(const std::string &xmlFile, const std::string &xmlRoot, TiXmlElement* &pNode)
{
  pNode = NULL;

  m_XML.Clear();

  if (!m_XML.LoadFile(xmlFile))
  {
    CLog::Log(LOGERROR, "{} Error loading {}, Line %d ({})", __FUNCTION__, xmlFile.c_str(), m_XML.ErrorRow(), m_XML.ErrorDesc());
    return;
  }
  TiXmlElement *pConfig = m_XML.RootElement();
  if (!pConfig || strcmpi(pConfig->Value(), xmlRoot.c_str()) != 0)
  {
    CLog::Log(LOGERROR, "{} Error loading madvrSettings, no <{}> node", __FUNCTION__, xmlRoot.c_str());
    return;
  }

  pNode = pConfig;
}

void CMadvrSettings::AddProfiles(TiXmlNode *pNode)
{
  if (pNode == NULL)
    return;

  TiXmlElement *pProfile = pNode->FirstChildElement("dsprofile");
  while (pProfile)
  {
    std::string strPath;
    std::string strFolders;

    if (CDSXMLUtils::GetString(pProfile, "path", &strPath) && CDSXMLUtils::GetString(pProfile, "folder", &strFolders))
      m_profiles[strPath] = strFolders;

    pProfile = pProfile->NextSiblingElement("dsprofile");
  }
}

void CMadvrSettings::AddSection(TiXmlNode *pNode, int iSectionId)
{
  if (pNode == NULL)
    return;

  m_iSubSectionId = iSectionId * MADVR_SECTION_SUB;
  if (m_iSubSectionId == 0)
    m_iSubSectionId = MADVR_SECTION_ROOT;

  int iSubSectionId = m_iSubSectionId;

  int iGroupId = 0;
  TiXmlElement *pGroup = pNode->FirstChildElement("group");
  while (pGroup)
  {
    iGroupId++;
    TiXmlNode *pSettingNode = pGroup->FirstChildElement();
    while (pSettingNode)
    {
      std::string strNode = pSettingNode->ValueStr();
      if (strNode == "setting")
      {
        AddSetting(pSettingNode, iSectionId, iGroupId);
      }
      else if (strNode == "section")
      {
        iSubSectionId++;
        std::string strSection = StringUtils::Format("section%i", iSubSectionId);
        AddButton(pSettingNode, iSectionId, iGroupId, iSubSectionId, "button_section", strSection);
        AddSection(pSettingNode, iSubSectionId);
      }
      else if (strNode == "debug")
      {
        m_bDebug = true;
        AddButton(pSettingNode, iSectionId, iGroupId, 0, "button_debug", "debug");
      }

      pSettingNode = pSettingNode->NextSibling();
    }
    pGroup = pGroup->NextSiblingElement("group");
  }
}

void CMadvrSettings::AddButton(TiXmlNode *pNode, int iSectionId, int iGroupId, int iSubSectionId, const std::string &type, const std::string &name)
{
  TiXmlElement *pSetting = pNode->ToElement();

  MadvrListSettings button;

  button.group = iGroupId;
  button.name = name;
  button.dialogId = NameToId(button.name);
  button.type = type;
  
  if (!CDSXMLUtils::GetInt(pSetting, "label",&button.label))
    CLog::Log(LOGERROR, "{} missing attritube (label) for button {}", __FUNCTION__, button.name.c_str());

  if (button.type == "button_section")
  {
    button.sectionId = iSubSectionId;
    m_sections[iSubSectionId].label = button.label;
    m_sections[iSubSectionId].parentId = iSectionId;
  }
  else if (button.type == "button_debug")
  {
    if (!CDSXMLUtils::GetString(pSetting,"path",&button.value))
      CLog::Log(LOGERROR, "{} missing attritube (path) for button {}", __FUNCTION__, button.name.c_str());
  }

  m_gui[iSectionId].emplace_back(std::move(button));
}

void CMadvrSettings::AddSetting(TiXmlNode *pNode, int iSectionId, int iGroupId)
{
  TiXmlElement *pSetting = pNode->ToElement();

  MadvrListSettings setting;
  CVariant pDefault;

  setting.group = iGroupId;

  //GET NAME, TYPE
  if (!CDSXMLUtils::GetString(pSetting, "name", &setting.name)
    ||!CDSXMLUtils::GetString(pSetting, "type", &setting.type))
  {
    CLog::Log(LOGERROR, "{} missing attritube (name, type) for setting name={} type={}", __FUNCTION__, setting.name.c_str(), setting.type.c_str());
  }
  if (StringUtils::StartsWith(setting.type, "!"))
  {
    StringUtils::Replace(setting.type, "!", "");
    setting.negate = true;
  }  

  // GET VALUE, PARENT
  CDSXMLUtils::GetString(pSetting, "value", &setting.value);
  CDSXMLUtils::GetString(pSetting, "parent", &setting.parent);
  setting.parent = NameToId(setting.parent);
  setting.dialogId = NameToId(setting.name);

  // GET LABEL
  if (!CDSXMLUtils::GetInt(pSetting, "label", &setting.label))
    CLog::Log(LOGERROR, "{} missing attritube (label) for setting name={}", __FUNCTION__, setting.name.c_str());

  // GET DEFAULT VALUE
  if (!GetVariant(pSetting, "default", setting.type, &pDefault))
    CLog::Log(LOGERROR, "{} missing attritube (default) for setting name={}", __FUNCTION__, setting.name.c_str());

  // GET DEPENDENCIES
  TiXmlElement *pDependencies = pSetting->FirstChildElement(SETTING_XML_ELM_DEPENDENCIES);
  if (pDependencies)
  { 
    TiXmlPrinter print;
    pDependencies->Accept(&print);
    setting.dependencies = DependenciesNameToId(print.CStr());
  }

  // GET OPTIONS
  if (setting.type.find("list_") != std::string::npos)
  {
    TiXmlElement *pOption = pSetting->FirstChildElement("option");
    while (pOption)
    {
      int iLabel;
      CVariant value;
      if (!CDSXMLUtils::GetInt(pOption, "label", &iLabel))
        CLog::Log(LOGERROR, "{} missing attritube (label) for setting option name={}", __FUNCTION__, setting.name.c_str());

      if (!GetVariant(pOption, "value", setting.type, &value))
        CLog::Log(LOGERROR, "{} missing attritube (value) for setting option name={}", __FUNCTION__, setting.name.c_str());

      if (value.isInteger())
      {
        setting.optionsInt.emplace_back(IntegerSettingOption(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(iLabel), value.asInteger()));
      }
      else
      {
        setting.optionsString.emplace_back(StringSettingOption(CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(iLabel), value.asString()));
      }

      // add options list
      m_options[setting.name].emplace_back(std::move(value));

      pOption = pOption->NextSiblingElement("option");
    }   
  }
  //GET FLOAT
  else if (setting.type == "float")
  {
    if (!CDSXMLUtils::GetString(pSetting,"format",&setting.slider.format))
      CLog::Log(LOGERROR, "{} missing attribute (format) for setting name={}", __FUNCTION__, setting.name.c_str());

    if ( !CDSXMLUtils::GetInt(pSetting, "parentlabel", &setting.slider.parentLabel)
      || !CDSXMLUtils::GetFloat(pSetting, "min", &setting.slider.min)
      || !CDSXMLUtils::GetFloat(pSetting, "max", &setting.slider.max)
      || !CDSXMLUtils::GetFloat(pSetting, "step", &setting.slider.step))
    {
      CLog::Log(LOGERROR, "{} missing attritube (parentLabel, min, max, step) for setting name={}", __FUNCTION__, setting.name.c_str());
    }
  }

   // ADD DATABASE
  m_db[setting.name] = pDefault;

  // ADD GUI
  m_gui[iSectionId].emplace_back(std::move(setting));
}

void CMadvrSettings::StoreAtStartSettings()
{
  CJSONVariantWriter::Write(m_db, m_madvrJsonAtStart, true);
}

void CMadvrSettings::RestoreDefaultSettings()
{
  m_db = m_dbDefault;
}

void CMadvrSettings::RestoreAtStartSettings()
{
   CJSONVariantParser::Parse(m_madvrJsonAtStart.c_str(), m_db);
}


bool CMadvrSettings::SettingsChanged()
{
  std::string strJson;
  CJSONVariantWriter::Write(m_db, strJson, true);
  return strJson != m_madvrJsonAtStart;
}

std::string CMadvrSettings::DependenciesNameToId(const std::string& dependencies)
{
  CRegExp reg(true);
  reg.RegComp("setting=\"([^\"]+)");
  char line[1024];
  std::string newDependencies;
  std::stringstream strStream(dependencies);

  while (strStream.getline(line, sizeof(line)))
  {
    std::string newLine = line;
    if (reg.RegFind(line) > -1)
    {
      std::string str = reg.GetMatch(1);
      StringUtils::Replace(newLine, str, NameToId(str));     
    }
    newDependencies = StringUtils::Format("{}{}\n", newDependencies.c_str(), newLine.c_str());
  }
  return newDependencies;
}

std::string CMadvrSettings::NameToId(const std::string &str)
{
  if (str.empty())
    return "";

  std::string sValue = StringUtils::Format("madvr.{}", str.c_str());
  StringUtils::ToLower(sValue);
  return sValue;
}

bool CMadvrSettings::GetVariant(TiXmlElement *pElement, const std::string &attr, const std::string &type, CVariant *variant)
{
  const char *str = pElement->Attribute(attr.c_str());
  if (str == NULL)
    return false;
  
  if (type == "list_int" || type == "list_boolint")
  {
    *variant = atoi(str);
  }
  else if (type == "bool" || type == "list_boolbool")
  {
    bool bValue = false;
    int iValue = -1;
    std::string strEnabled = str;
    StringUtils::ToLower(strEnabled);
    if (strEnabled == "false" || strEnabled == "0")
    {
      bValue = false;
      iValue = 0;
    }
    else if (strEnabled == "true" || strEnabled == "1")
    {
      bValue = true;
      iValue = 1;
    }
    type == "bool" ? *variant = bValue : *variant = iValue;
  }
  else if (type == "float")
  {
    *variant = (float)atof(str);
  }
  else
  {
    *variant = std::string(str);
  }

  return true;
}

std::string CMadvrSettings::GetVersionSuffix(const std::string &path)
{
  if (path.empty())
    return "";
  
  std::string sVersion;

  TiXmlElement *pVersions;
  LoadMadvrXML(path+"versions.xml", "versions", pVersions);
  if (pVersions)
  {
    TiXmlElement *pVersion = pVersions->FirstChildElement("version");
    while (pVersion)
    {
      std::string sMin, sMax;
      unsigned int iMin, iMax;
      CDSXMLUtils::GetString(pVersion, "min", &sMin);
      CDSXMLUtils::GetString(pVersion, "max", &sMax);
      std::vector<std::string> vecMin = StringUtils::Split(sMin, ".");
      std::vector<std::string> vecMax = StringUtils::Split(sMax, ".");

      if (vecMin.size() != 4 || vecMax.size() != 4)
        continue;

      iMin = (atoi(vecMin[0].c_str()) << 24 | atoi(vecMin[1].c_str()) << 16 | atoi(vecMin[2].c_str()) << 8 | atoi(vecMin[3].c_str()));
      iMax = (atoi(vecMax[0].c_str()) << 24 | atoi(vecMax[1].c_str()) << 16 | atoi(vecMax[2].c_str()) << 8 | atoi(vecMax[3].c_str()));
      unsigned int iCurrentVersion = CDSFilterVersion::Get()->GetIntVersion(CGraphFilters::MADSHI_VIDEO_RENDERER);

      if (iCurrentVersion >= iMin && iCurrentVersion <= iMax)
      {
        CDSXMLUtils::GetString(pVersion, "id", &sVersion);
        break;
      }

      pVersion = pVersion->NextSiblingElement("version");
    }
  }

  return sVersion;
}
