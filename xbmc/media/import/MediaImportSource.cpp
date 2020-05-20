/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportSource.h"

CMediaImportSource::CMediaImportSource(const std::string& identifier /* = "" */,
                                       const std::string& basePath /* = "" */,
                                       const std::string& friendlyName /* = "" */,
                                       const std::string& iconUrl /* = "" */,
                                       const MediaTypes& availableMediaTypes /* = MediaTypes() */,
                                       const CDateTime& lastSynced /* = CDateTime() */,
                                       const std::string& settingValues /* = "" */,
                                       bool manuallyAdded /* = false */,
                                       const std::string& importerId /* = "" */)
  : m_identifier(identifier),
    m_basePath(basePath),
    m_friendlyName(friendlyName),
    m_iconUrl(iconUrl),
    m_availableMediaTypes(availableMediaTypes),
    m_lastSynced(lastSynced),
    m_manuallyAdded(manuallyAdded),
    m_settings(std::make_shared<CMediaImportSettingsBase>(settingValues)),
    m_importerId(importerId),
    m_active(false),
    m_ready(false)
{
}

CMediaImportSource::CMediaImportSource(const CMediaImportSource& other)
  : m_identifier(other.m_identifier),
    m_basePath(other.m_basePath),
    m_friendlyName(other.m_friendlyName),
    m_iconUrl(other.m_iconUrl),
    m_availableMediaTypes(other.m_availableMediaTypes),
    m_lastSynced(other.m_lastSynced),
    m_manuallyAdded(other.m_manuallyAdded),
    m_settings(other.m_settings),
    m_importerId(other.m_importerId),
    m_active(other.m_active),
    m_ready(other.m_ready)
{
}

bool CMediaImportSource::operator==(const CMediaImportSource& other) const
{
  if (m_identifier.compare(other.m_identifier) != 0 || m_basePath.compare(other.m_basePath) != 0 ||
      m_friendlyName.compare(other.m_friendlyName) != 0 ||
      m_iconUrl.compare(other.m_iconUrl) != 0 ||
      m_availableMediaTypes != other.m_availableMediaTypes ||
      m_manuallyAdded != other.m_manuallyAdded || *m_settings != *other.m_settings)
    return false;

  return true;
}

CMediaImportSource CMediaImportSource::Clone() const
{
  CMediaImportSource clone(*this);

  // deep-copy the settings
  clone.m_settings = std::make_shared<CMediaImportSettingsBase>(*m_settings);

  return clone;
}

std::ostream& operator<<(std::ostream& os, const CMediaImportSource& source)
{
  return os << "\"" << source.GetFriendlyName() << "\""
            << " (" << source.GetIdentifier() << ")";
}
