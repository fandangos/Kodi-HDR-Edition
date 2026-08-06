#pragma once

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

#include "settings/dialogs/GUIDialogSettingsManualBase.h"
#include "utils/XMLUtils.h"


enum ConfigType {
  EDITATTR,
  EDITATTREXTRA,
  EDITATTRSHADER,
  SPINNERATTR,
  SPINNERATTRSHADER,
  BOOLATTR,
  FILTER,
  EXTRAFILTER,
  SHADER,
  OSDGUID,
  FILTERSYSTEM
};

enum xmlType {
  MEDIASCONFIG,
  FILTERSCONFIG,
  HOMEFILTERSCONFIG,
  SHADERS,
  PLAYERCOREFACTORY
};

class DSConfigList
{
public:
  DSConfigList(ConfigType type);

  std::string m_attr;
  std::string m_nodeName;
  std::string m_nodeList;
  std::string m_value;
  std::string m_setting;
  int m_subNode;
  int m_label;
  ConfigType m_configType;
  StringSettingOptionsFiller m_filler;
  bool GetBoolValue() { return m_value == "true" ? true : false; }
  void SetBoolValue(bool value) { value ? m_value = "true" : m_value = "false"; }
};

class CGUIDialogDSManager
{
public:
  CGUIDialogDSManager();
  virtual ~CGUIDialogDSManager();

  static CGUIDialogDSManager* Get();
  static void Destroy()
  {
    delete m_pSingleton;
    m_pSingleton = NULL;
  }

  bool GetNew() { return m_bNew; };  
  void SetConfig(bool bNew, int iIndex)  { m_bNew = bNew;  m_iIndex = iIndex; };

  void InitConfig(std::vector<DSConfigList *> &configList, ConfigType type, const std::string &strSetting, int label, const std::string &strAttr = "", const std::string &strNodeName = "", StringSettingOptionsFiller filler = NULL, int subNode = 0, const std::string &strNodeList = "");
  void ResetValue(std::vector<DSConfigList *>& configList);
  void LoadDsXML(xmlType type, TiXmlElement* &pNode, bool forceCreate = false);
  void SaveDsXML(xmlType type);
  void GetPath(xmlType type, std::string &xmlFile, std::string &xmlNode, std::string &xmlRoot);
  static void AllFiltersConfigOptionFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current);
  static void ShadersOptionFiller(const std::shared_ptr<const CSetting>& setting,
    StringSettingOptions& list,
    std::string& current);
  static void ShadersScaleOptionFiller(const std::shared_ptr<const CSetting>& setting,
    StringSettingOptions& list,
    std::string& current);
  static void DSFilterOptionFiller(const std::shared_ptr<const CSetting>& setting, StringSettingOptions& list, std::string& current);
  static void BoolOptionFiller(std::shared_ptr<const CSetting>& setting, std::vector< std::pair<std::string, std::string> > &list, std::string &current);
  static void PriorityOptionFiller(const std::shared_ptr<const CSetting>& setting, StringSettingOptions& list, std::string& current);
  static bool compare_by_word(const StringSettingOption& lhs, const StringSettingOption& rhs);
  void GetFilterList(xmlType type, std::vector<StringSettingOption> &list);
  TiXmlElement* KeepSelectedNode(TiXmlElement* pNode, const std::string &subNodeName);
  bool FindPrepend(TiXmlElement* &pNode, const std::string &xmlNode);

protected:

  static CGUIDialogDSManager* m_pSingleton;

  bool m_bNew;
  int m_iIndex;
  CXBMCTinyXML m_XML;
};
