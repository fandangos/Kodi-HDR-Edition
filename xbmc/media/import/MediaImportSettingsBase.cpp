/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportSettingsBase.h"

#include "settings/lib/SettingsManager.h"
#include "utils/XBMCTinyXML.h"
#include "utils/log.h"

CMediaImportSettingsBase::CMediaImportSettingsBase(const std::string& settingValues /* = "" */)
  : CSettingsBase(), CStaticLoggerBase("CMediaImportSettingsBase"), m_settingValues(settingValues)
{
}

CMediaImportSettingsBase::CMediaImportSettingsBase(const CMediaImportSettingsBase& other)
  : CMediaImportSettingsBase(other.m_settingValues)
{
  m_settingDefinitions = other.m_settingDefinitions;
  m_simpleConditions = other.m_simpleConditions;
  // ATTENTION: don't copy complex conditions because they may contain instance specific userdata

  if (other.IsLoaded())
    Load();
}

bool CMediaImportSettingsBase::operator==(const CMediaImportSettingsBase& other) const
{
  if (ToXml() != other.ToXml())
    return false;

  if (IsLoaded() != other.IsLoaded())
    return true;

  return m_settingDefinitions == other.m_settingDefinitions;
}

CMediaImportSettingsBase& CMediaImportSettingsBase::operator=(const CMediaImportSettingsBase& other)
{
  if (this == &other)
    return *this;

  if (IsLoaded())
    Unload();

  m_settingValues = other.m_settingValues;
  m_settingDefinitions = other.m_settingDefinitions;

  if (other.IsLoaded())
    Load();

  return *this;
}

bool CMediaImportSettingsBase::Load()
{
  // try to initialize the settings by loading its definitions
  if (!Initialize())
  {
    s_logger->error("failed to initialize settings");
    return false;
  }

  // if available try to load the setting's values
  if (!m_settingValues.empty())
  {
    CXBMCTinyXML xmlValues;
    if (!xmlValues.Parse(m_settingValues, TIXML_ENCODING_UTF8))
    {
      s_logger->error("error loading setting values, Line {}\n{}", xmlValues.ErrorRow(),
                      xmlValues.ErrorDesc());
      Uninitialize();
      return false;
    }

    bool updated;
    if (!LoadValuesFromXml(xmlValues, updated))
    {
      s_logger->error("failed to load setting values");
      Uninitialize();
      return false;
    }
  }

  // we are done with loading
  SetLoaded();

  return true;
}

bool CMediaImportSettingsBase::Save()
{
  if (!IsLoaded())
    return false;

  m_settingValues = ToXml();
  return true;
}

void CMediaImportSettingsBase::Unload()
{
  CSettingsBase::Unload();
  CSettingsBase::Uninitialize();
}

bool CMediaImportSettingsBase::HasDefinition(const std::string& settingDefinition) const
{
  return m_settingDefinitions.find(settingDefinition) != m_settingDefinitions.end();
}

void CMediaImportSettingsBase::AddDefinition(const std::string& settingDefinition)
{
  m_settingDefinitions.insert(settingDefinition);
}

void CMediaImportSettingsBase::AddSimpleCondition(const std::string& condition)
{
  m_simpleConditions.insert(condition);
}

void CMediaImportSettingsBase::AddComplexCondition(const std::string& name,
                                                   const SettingConditionCheck& condition,
                                                   void* data /* = nullptr */)
{
  m_complexConditions.emplace(name, std::make_tuple(condition, data));
}

void CMediaImportSettingsBase::SetOptionsFiller(const std::string& settingId,
                                                IntegerSettingOptionsFiller optionsFiller,
                                                void* data /* = nullptr */)
{
  if (!IsLoaded() || settingId.empty())
    return;

  auto setting = GetSetting(settingId);
  if (setting == nullptr)
    return;

  if (setting->GetType() == SettingType::List)
    setting = std::static_pointer_cast<CSettingList>(setting)->GetDefinition();

  if (setting->GetType() != SettingType::Integer)
    return;

  std::static_pointer_cast<CSettingInt>(setting)->SetOptionsFiller(optionsFiller, data);
}

void CMediaImportSettingsBase::SetOptionsFiller(const std::string& settingId,
                                                StringSettingOptionsFiller optionsFiller,
                                                void* data /* = nullptr */)
{
  if (!IsLoaded() || settingId.empty())
    return;

  auto setting = GetSetting(settingId);
  if (setting == nullptr)
    return;

  if (setting->GetType() == SettingType::List)
    setting = std::static_pointer_cast<CSettingList>(setting)->GetDefinition();

  if (setting->GetType() != SettingType::String)
    return;

  std::static_pointer_cast<CSettingString>(setting)->SetOptionsFiller(optionsFiller, data);
}

std::string CMediaImportSettingsBase::ToXml() const
{
  if (!IsLoaded())
    return m_settingValues;

  CXBMCTinyXML xmlValues;
  if (!SaveValuesToXml(xmlValues))
    return m_settingValues;

  TiXmlPrinter printer;
  xmlValues.Accept(&printer);

  return printer.Str();
}

bool CMediaImportSettingsBase::InitializeDefinitions()
{
  if (m_settingDefinitions.empty())
    return true;

  for (const auto& settingDefinition : m_settingDefinitions)
  {
    CXBMCTinyXML xmlDefinition;
    if (!xmlDefinition.Parse(settingDefinition, TIXML_ENCODING_UTF8))
    {
      s_logger->error("error loading settings definition, Line {}\n{}", xmlDefinition.ErrorRow(),
                      xmlDefinition.ErrorDesc());
      return false;
    }

    if (xmlDefinition.RootElement() == nullptr)
      return false;

    if (!InitializeDefinitionsFromXml(xmlDefinition))
      return false;
  }

  return true;
}

void CMediaImportSettingsBase::InitializeControls()
{
  GetSettingsManager()->RegisterSettingControl("toggle", this);
  GetSettingsManager()->RegisterSettingControl("spinner", this);
  GetSettingsManager()->RegisterSettingControl("edit", this);
  GetSettingsManager()->RegisterSettingControl("button", this);
  GetSettingsManager()->RegisterSettingControl("list", this);
  GetSettingsManager()->RegisterSettingControl("slider", this);
  GetSettingsManager()->RegisterSettingControl("range", this);
  GetSettingsManager()->RegisterSettingControl("title", this);
}

void CMediaImportSettingsBase::InitializeConditions()
{
  // add simple conditions
  for (const auto condition : m_simpleConditions)
    GetSettingsManager()->AddCondition(condition);

  // add more complex conditions
  for (const auto condition : m_complexConditions)
    GetSettingsManager()->AddDynamicCondition(condition.first, std::get<0>(condition.second),
                                              std::get<1>(condition.second));
}
