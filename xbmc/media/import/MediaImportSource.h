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
#include "media/import/MediaImportSettingsBase.h"

#include <memory>
#include <ostream>
#include <set>
#include <string>

using MediaImportSourceSettingsPtr = MediaImportSettingsBasePtr;
using MediaImportSourceSettingsConstPtr = std::shared_ptr<const CMediaImportSettingsBase>;

class CMediaImportSource
{
public:
  explicit CMediaImportSource(const std::string& identifier = "",
                              const std::string& basePath = "",
                              const std::string& friendlyName = "",
                              const std::string& iconUrl = "",
                              const MediaTypes& availableMediaTypes = MediaTypes(),
                              const CDateTime& lastSynced = CDateTime(),
                              const std::string& settingValues = "",
                              bool manuallyAdded = false,
                              const std::string& importerId = "");
  CMediaImportSource(const CMediaImportSource& other);

  ~CMediaImportSource() = default;

  bool operator==(const CMediaImportSource& other) const;
  bool operator!=(const CMediaImportSource& other) const { return !(*this == other); }

  CMediaImportSource Clone() const;

  const std::string& GetIdentifier() const { return m_identifier; }
  void SetIdentifier(const std::string& identifier) { m_identifier = identifier; }

  const std::string& GetBasePath() const { return m_basePath; }
  void SetBasePath(const std::string& basePath) { m_basePath = basePath; }

  const std::string& GetFriendlyName() const { return m_friendlyName; }
  void SetFriendlyName(const std::string& friendlyName) { m_friendlyName = friendlyName; }

  const std::string& GetIconUrl() const { return m_iconUrl; }
  void SetIconUrl(const std::string& iconUrl) { m_iconUrl = iconUrl; }

  const MediaTypes& GetAvailableMediaTypes() const { return m_availableMediaTypes; }
  void SetAvailableMediaTypes(const MediaTypes& mediaTypes) { m_availableMediaTypes = mediaTypes; }
  bool IsMediaTypeAvailable(const MediaTypes::value_type& mediaType) const
  {
    return m_availableMediaTypes.find(mediaType) != m_availableMediaTypes.cend();
  }

  const CDateTime& GetLastSynced() const { return m_lastSynced; }
  void SetLastSynced(const CDateTime& lastSynced) { m_lastSynced = lastSynced; }

  bool IsManuallyAdded() const { return m_manuallyAdded; }
  void SetManuallyAdded(bool manuallyAdded) { m_manuallyAdded = manuallyAdded; }

  MediaImportSourceSettingsConstPtr Settings() const { return m_settings; }
  MediaImportSourceSettingsPtr Settings() { return m_settings; }

  const std::string& GetImporterId() const { return m_importerId; }
  void SetImporterId(const std::string& importerId) { m_importerId = importerId; }

  bool IsActive() const { return m_active; }
  void SetActive(bool active) { m_active = active; }

  bool IsReady() const { return m_ready; }
  void SetReady(bool ready) { m_ready = ready; }

  friend std::ostream& operator<<(std::ostream& os, const CMediaImportSource& source);

private:
  std::string m_identifier;
  std::string m_basePath;
  std::string m_friendlyName;
  std::string m_iconUrl;
  MediaTypes m_availableMediaTypes;
  CDateTime m_lastSynced;
  bool m_manuallyAdded;
  MediaImportSourceSettingsPtr m_settings;
  std::string m_importerId;

  bool m_active;
  bool m_ready;
};

using MediaImportSourcePtr = std::shared_ptr<CMediaImportSource>;
