/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "XBDateTime.h"
#include "media/MediaType.h"
#include "media/import/MediaImportSource.h"

#include <memory>
#include <set>
#include <string>

enum class MediaImportTrigger
{
  Auto = 0,
  Manual = 1
};

class CMediaImportSettings : public CMediaImportSettingsBase
{
public:
  explicit CMediaImportSettings(const GroupedMediaTypes& mediaTypes,
                                const std::string& settingValues = "");
  CMediaImportSettings(const CMediaImportSettings& other);
  virtual ~CMediaImportSettings() = default;

  MediaImportTrigger GetImportTrigger() const;
  bool SetImportTrigger(MediaImportTrigger importTrigger);
  bool UpdateImportedMediaItems() const;
  bool SetUpdateImportedMediaItems(bool updateImportedMediaItems);
  bool UpdatePlaybackMetadataFromSource() const;
  bool SetUpdatePlaybackMetadataFromSource(bool updatePlaybackMetadataFromSource);
  bool UpdatePlaybackMetadataOnSource() const;
  bool SetUpdatePlaybackMetadataOnSource(bool updatePlaybackMetadataOnSource);

  static const std::string SettingTrigger;
  static const std::string SettingTriggerValueAuto;
  static const std::string SettingTriggerValueManual;
  static const std::string SettingUpdateItems;
  static const std::string SettingUpdatePlaybackMetadataFromSource;
  static const std::string SettingUpdatePlaybackMetadataOnSource;

private:
  static const std::string SettingsDefinition;

  static const std::string SettingConditionHasMediaType;

  void Setup();

  static bool HasMediaType(const std::string& condition,
                           const std::string& value,
                           std::shared_ptr<const CSetting> setting,
                           void* data);

  const GroupedMediaTypes m_mediaTypes;
};

using MediaImportSettingsPtr = std::shared_ptr<CMediaImportSettings>;
using MediaImportSettingsConstPtr = std::shared_ptr<const CMediaImportSettings>;

class CMediaImport
{
public:
  explicit CMediaImport(const std::string& importPath = "");
  CMediaImport(const std::string& importPath, const CMediaImportSource& source);
  CMediaImport(const CMediaImport& other);

  ~CMediaImport() = default;

  static CMediaImport CreateRecursive(const std::string& importPath,
                                      const GroupedMediaTypes& importedMediaTypes,
                                      const CMediaImportSource& source,
                                      const CDateTime& lastSynced = CDateTime(),
                                      const std::string& settingValues = "")
  {
    return CMediaImport(importPath, source, importedMediaTypes, true, lastSynced, settingValues);
  }
  static CMediaImport CreateSelective(const std::string& importPath,
                                      const GroupedMediaTypes& importedMediaTypes,
                                      const CMediaImportSource& source,
                                      const CDateTime& lastSynced = CDateTime(),
                                      const std::string& settingValues = "")
  {
    return CMediaImport(importPath, source, importedMediaTypes, false, lastSynced, settingValues);
  }

  bool operator==(const CMediaImport& other) const;
  bool operator!=(const CMediaImport& other) const { return !(*this == other); }

  CMediaImport Clone() const;

  const std::string& GetPath() const { return m_importPath; }

  CMediaImportSource& GetSource() { return m_source; }
  const CMediaImportSource& GetSource() const { return m_source; }
  void SetSource(const CMediaImportSource& source)
  {
    if (source.GetIdentifier().empty())
      return;

    m_source = source;
  }

  const GroupedMediaTypes& GetMediaTypes() const { return m_mediaTypes; }
  void SetMediaTypes(const GroupedMediaTypes& mediaTypes) { m_mediaTypes = mediaTypes; }
  bool ContainsMediaType(const GroupedMediaTypes::value_type mediaType) const;

  bool IsRecursive() const { return m_recursive; }
  void SetRecursive(bool recursive) { m_recursive = recursive; }

  const CDateTime& GetLastSynced() const { return m_lastSynced; }
  void SetLastSynced(const CDateTime& lastSynced)
  {
    m_lastSynced = lastSynced;
    m_source.SetLastSynced(lastSynced);
  }

  MediaImportSettingsConstPtr Settings() const { return m_settings; }
  MediaImportSettingsPtr Settings() { return m_settings; }

  bool IsActive() const { return m_source.IsActive(); }
  void SetActive(bool active) { m_source.SetActive(active); }

  bool IsReady() const { return m_source.IsReady(); }
  void SetReady(bool ready) { m_source.SetReady(ready); }

  friend std::ostream& operator<<(std::ostream& os, const CMediaImport& import);

private:
  CMediaImport(const std::string& importPath,
               const CMediaImportSource& source,
               const GroupedMediaTypes& importedMediaTypes,
               bool recursive,
               const CDateTime& lastSynced,
               const std::string& settingValues);

  std::string m_importPath;
  GroupedMediaTypes m_mediaTypes;
  CMediaImportSource m_source;
  bool m_recursive;
  CDateTime m_lastSynced;
  MediaImportSettingsPtr m_settings;
};

using MediaImportPtr = std::shared_ptr<CMediaImport>;
