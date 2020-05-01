/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaProvider.h"

#include "ServiceBroker.h"
#include "interfaces/legacy/LanguageHook.h"
#include "interfaces/legacy/mediaimport/Exceptions.h"
#include "interfaces/legacy/mediaimport/MediaImport.h"
#include "media/import/MediaImportManager.h"

#include <memory>

namespace XBMCAddon
{
namespace xbmcmediaimport
{
#ifndef SWIG
MediaProvider::MediaProvider(const std::string& addonId /* = "" */,
                             CAddonMediaImporter* addonMediaImporter /* = nullptr */)
  : MediaProvider(std::make_shared<CMediaImportSource>(""), addonId, addonMediaImporter)
{
}

MediaProvider::MediaProvider(MediaImportSourcePtr source,
                             const std::string& addonId /* = "" */,
                             CAddonMediaImporter* addonMediaImporter /* = nullptr */)
  : source(source), m_addonId(addonId), m_addonMediaImporter(addonMediaImporter)
{
  if (m_addonId.empty())
  {
    if (languageHook != nullptr)
      m_addonId = languageHook->GetAddonId();

    if (m_addonId.empty())
      throw MissingLanguageHookException("MediaProvider");
  }

  m_callbackData.type = CAddonMediaImporter::SettingsCallbackData::Source;
  m_callbackData.data.source = source.get();
}

MediaProvider::MediaProvider(const CMediaImportSource& source,
                             const std::string& addonId /* = "" */,
                             CAddonMediaImporter* addonMediaImporter /* = nullptr */)
  : MediaProvider(std::make_shared<CMediaImportSource>(source), addonId, addonMediaImporter)
{
}
#endif

MediaProvider::MediaProvider(const String& identifier,
                             const String& basePath /* = emptyString */,
                             const String& friendlyName /* = emptyString */,
                             const String& iconUrl /* = emptyString */,
                             const MediaTypes& mediaTypes /* = {} */,
                             const String& lastSynced /* = emptyString */,
                             int handle /* = -1 */)
  : source(std::make_shared<CMediaImportSource>(
        identifier, basePath, friendlyName, iconUrl, mediaTypes)),
    m_addonMediaImporter(nullptr)
{
  if (languageHook != nullptr)
    m_addonId = languageHook->GetAddonId();

  if (m_addonId.empty())
    throw MissingLanguageHookException("MediaProvider");

  // properly set last synced
  if (!lastSynced.empty())
  {
    CDateTime dtLastSynced;
    dtLastSynced.SetFromDBDateTime(lastSynced);
    if (dtLastSynced.IsValid())
      source->SetLastSynced(dtLastSynced);
  }

  if (handle >= 0)
  {
    m_addonMediaImporter = CAddonMediaImporter::GetImporter(handle);
    m_callbackData.type = CAddonMediaImporter::SettingsCallbackData::Source;
    m_callbackData.data.source = source.get();
  }
}

String MediaProvider::getIdentifier() const
{
  return source->GetIdentifier();
}

String MediaProvider::getBasePath() const
{
  return source->GetBasePath();
}

void MediaProvider::setBasePath(const String& basePath)
{
  source->SetBasePath(basePath);
}

String MediaProvider::getFriendlyName() const
{
  return source->GetFriendlyName();
}

void MediaProvider::setFriendlyName(const String& friendlyName)
{
  source->SetFriendlyName(friendlyName);
}

String MediaProvider::getIconUrl() const
{
  return source->GetIconUrl();
}

void MediaProvider::setIconUrl(const String& iconUrl)
{
  source->SetIconUrl(iconUrl);
}

MediaTypes MediaProvider::getAvailableMediaTypes() const
{
  return source->GetAvailableMediaTypes();
}

void MediaProvider::setAvailableMediaTypes(const MediaTypes& mediaTypes)
{
  source->SetAvailableMediaTypes(mediaTypes);
}

String MediaProvider::getLastSynced() const
{
  if (!source->GetLastSynced().IsValid())
    return emptyString;

  return source->GetLastSynced().GetAsW3CDateTime(true);
}

XBMCAddon::xbmcaddon::Settings* MediaProvider::prepareSettings()
{
  auto settings = source->Settings();
  if (CAddonMediaImporter::PrepareProviderSettings(m_addonId, settings))
    return createSettings(settings);

  return nullptr;
}

XBMCAddon::xbmcaddon::Settings* MediaProvider::getSettings()
{
  return createSettings(source->Settings());
}

bool MediaProvider::isActive() const
{
  return source->IsActive();
}

std::vector<MediaImport*> MediaProvider::getImports() const
{
  const auto imports =
      CServiceBroker::GetMediaImportManager().GetImportsBySource(source->GetIdentifier());

  std::vector<MediaImport*> result;
  for (const auto& import : imports)
  {
    result.push_back(new MediaImport(import, m_addonId, m_addonMediaImporter));
  }
  return result;
}

XBMCAddon::xbmcaddon::Settings* MediaProvider::createSettings(MediaImportSourceSettingsPtr settings)
{
  if (settings == nullptr)
    return nullptr;

  return new xbmcaddon::Settings(settings, m_addonId, m_addonMediaImporter, &m_callbackData);
}
} // namespace xbmcmediaimport
} // namespace XBMCAddon
