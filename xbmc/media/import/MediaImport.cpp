/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImport.h"

#include "utils/StringUtils.h"

#include <algorithm>

const std::string CMediaImportSettings::SettingTrigger = "sync.importtrigger";
const std::string CMediaImportSettings::SettingTriggerValueAuto = "auto";
const std::string CMediaImportSettings::SettingTriggerValueManual = "manual";
const std::string CMediaImportSettings::SettingUpdateItems = "sync.updateimporteditems";
const std::string CMediaImportSettings::SettingUpdatePlaybackMetadataFromSource =
    "sync.updateplaybackmetadatafromsource";
const std::string CMediaImportSettings::SettingUpdatePlaybackMetadataOnSource =
    "sync.updateplaybackmetadataonsource";

const std::string CMediaImportSettings::SettingsDefinition = R"(
<?xml version="1.0" encoding="utf-8" ?>
<settings version="1">
  <section id="import" label="16001">
    <category id="sync" label="39530">
      <group id="1">
        <setting id=")" + SettingTrigger + R"(" type="string" label="39534">
          <level>0</level>
          <default>)" + SettingTriggerValueAuto + R"(</default>
          <constraints>
            <allowempty>false</allowempty>
            <options>
              <option label="39535">)" + SettingTriggerValueAuto +
                                                             R"(</option>
              <option label="39536">)" + SettingTriggerValueManual +
                                                             R"(</option>
            </options>
          </constraints>
          <control type="spinner" format="string" />
        </setting>
      </group>
      <group id="2">
        <setting id=")" + SettingUpdateItems + R"(" type="boolean" label="39531">
          <level>0</level>
          <default>true</default>
          <control type="toggle" />
        </setting>
        <setting id=")" + SettingUpdatePlaybackMetadataFromSource +
                                                             R"(" type="boolean" parent=")" +
                                                             SettingUpdateItems +
                                                             R"(" label="39532">
          <level>0</level>
          <default>true</default>
          <dependencies>
            <dependency type="enable" setting=")" + SettingUpdateItems +
                                                             R"(">true</dependency>
          </dependencies>
          <control type="toggle" />
        </setting>
        <setting id=")" + SettingUpdatePlaybackMetadataOnSource +
                                                             R"(" type="boolean" label="39533">
          <level>0</level>
          <default>true</default>
          <control type="toggle" />
        </setting>
      </group>
    </category>
  </section>
</settings>
)";

const std::string CMediaImportSettings::SettingConditionHasMediaType = "hasmediatype";

CMediaImportSettings::CMediaImportSettings(const GroupedMediaTypes& mediaTypes,
                                           const std::string& settingValues /* = "" */)
  : CMediaImportSettingsBase(settingValues), m_mediaTypes(mediaTypes)
{
  Setup();
}

CMediaImportSettings::CMediaImportSettings(const CMediaImportSettings& other)
  : CMediaImportSettingsBase(other), m_mediaTypes(other.m_mediaTypes)
{
  Setup();
}

MediaImportTrigger CMediaImportSettings::GetImportTrigger() const
{
  if (!IsLoaded())
    return MediaImportTrigger::Auto;

  const auto trigger = GetString(SettingTrigger);
  if (trigger == SettingTriggerValueManual)
    return MediaImportTrigger::Manual;

  return MediaImportTrigger::Auto;
}

bool CMediaImportSettings::SetImportTrigger(MediaImportTrigger importTrigger)
{
  if (!IsLoaded())
    return false;

  std::string trigger = SettingTriggerValueAuto;
  switch (importTrigger)
  {
    case MediaImportTrigger::Manual:
      trigger = SettingTriggerValueManual;
      break;

    case MediaImportTrigger::Auto:
    default:
      trigger = SettingTriggerValueAuto;
      break;
  }

  return SetString(SettingTrigger, trigger);
}

bool CMediaImportSettings::UpdateImportedMediaItems() const
{
  if (!IsLoaded())
    return true;

  return GetBool(SettingUpdateItems);
}

bool CMediaImportSettings::SetUpdateImportedMediaItems(bool updateImportedMediaItems)
{
  if (!IsLoaded())
    return false;

  return SetBool(SettingUpdateItems, updateImportedMediaItems);
}

bool CMediaImportSettings::UpdatePlaybackMetadataFromSource() const
{
  if (!IsLoaded())
    return true;

  return GetBool(SettingUpdatePlaybackMetadataFromSource);
}

bool CMediaImportSettings::SetUpdatePlaybackMetadataFromSource(
    bool updatePlaybackMetadataFromSource)
{
  if (!IsLoaded())
    return false;

  return SetBool(SettingUpdatePlaybackMetadataFromSource, updatePlaybackMetadataFromSource);
}

bool CMediaImportSettings::UpdatePlaybackMetadataOnSource() const
{
  if (!IsLoaded())
    return true;

  return GetBool(SettingUpdatePlaybackMetadataOnSource);
}

bool CMediaImportSettings::SetUpdatePlaybackMetadataOnSource(bool updatePlaybackMetadataOnSource)
{
  if (!IsLoaded())
    return false;

  return SetBool(SettingUpdatePlaybackMetadataOnSource, updatePlaybackMetadataOnSource);
}

void CMediaImportSettings::Setup()
{
  AddDefinition(SettingsDefinition);

  AddComplexCondition(SettingConditionHasMediaType, HasMediaType, this);
}

bool CMediaImportSettings::HasMediaType(const std::string& condition,
                                        const std::string& value,
                                        std::shared_ptr<const CSetting> setting,
                                        void* data)
{
  if (data == nullptr)
    return false;

  auto mediaType = value;
  StringUtils::ToLower(mediaType);

  auto mediaImportSettings = reinterpret_cast<CMediaImportSettings*>(data);
  const auto& mediaTypes = mediaImportSettings->m_mediaTypes;
  if (std::find(mediaTypes.begin(), mediaTypes.end(), mediaType) == mediaTypes.end())
    return false;

  return true;
}

CMediaImport::CMediaImport(const std::string& importPath /* = "" */)
  : CMediaImport(importPath, CMediaImportSource(importPath))
{
}

CMediaImport::CMediaImport(const std::string& importPath, const CMediaImportSource& source)
  : CMediaImport(importPath, source, {}, true, CDateTime(), "")
{
}

CMediaImport::CMediaImport(const std::string& importPath,
                           const CMediaImportSource& source,
                           const GroupedMediaTypes& importedMediaTypes,
                           bool recursive,
                           const CDateTime& lastSynced,
                           const std::string& settingValues)
  : m_importPath(importPath),
    m_mediaTypes(importedMediaTypes),
    m_source(source),
    m_recursive(recursive),
    m_lastSynced(lastSynced),
    m_settings(std::make_shared<CMediaImportSettings>(m_mediaTypes, settingValues))
{
}

CMediaImport::CMediaImport(const CMediaImport& other)
  : m_importPath(other.m_importPath),
    m_mediaTypes(other.m_mediaTypes),
    m_source(other.m_source),
    m_recursive(other.m_recursive),
    m_lastSynced(other.m_lastSynced),
    m_settings(other.m_settings)
{
}

bool CMediaImport::operator==(const CMediaImport& other) const
{
  if (m_importPath.compare(other.m_importPath) != 0 || m_source != other.m_source ||
      m_mediaTypes != other.m_mediaTypes || m_recursive != other.m_recursive ||
      m_lastSynced != other.m_lastSynced || *m_settings != *other.m_settings)
    return false;

  return true;
}

CMediaImport CMediaImport::Clone() const
{
  CMediaImport clone(*this);

  // deep-copy the source and the settings
  clone.m_source = m_source.Clone();
  clone.m_settings = std::make_shared<CMediaImportSettings>(*m_settings);

  return clone;
}

bool CMediaImport::ContainsMediaType(const GroupedMediaTypes::value_type mediaType) const
{
  return std::find(m_mediaTypes.cbegin(), m_mediaTypes.cend(), mediaType) != m_mediaTypes.cend();
}

std::ostream& operator<<(std::ostream& os, const CMediaImport& import)
{
  return os << import.GetPath() << " (" << StringUtils::Join(import.GetMediaTypes(), ", ") << ")";
}
