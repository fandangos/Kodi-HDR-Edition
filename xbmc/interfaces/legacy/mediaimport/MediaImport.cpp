/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImport.h"

#include "interfaces/legacy/LanguageHook.h"
#include "interfaces/legacy/mediaimport/Exceptions.h"
#include "interfaces/legacy/mediaimport/MediaProvider.h"

#include <memory>

namespace XBMCAddon
{
namespace xbmcmediaimport
{
#ifndef SWIG
MediaImport::MediaImport(MediaImportPtr import,
                         const std::string& addonId /* = "" */,
                         CAddonMediaImporter* addonMediaImporter /* = nullptr */)
  : import(import), m_addonId(addonId), m_addonMediaImporter(addonMediaImporter)
{
  if (m_addonId.empty())
  {
    if (languageHook != nullptr)
      m_addonId = languageHook->GetAddonId();

    if (m_addonId.empty())
      throw MissingLanguageHookException("MediaImport");
  }

  m_callbackData.type = CAddonMediaImporter::SettingsCallbackData::Import;
  m_callbackData.data.import = import.get();
}

MediaImport::MediaImport(const CMediaImport& import,
                         const std::string& addonId /* = "" */,
                         CAddonMediaImporter* addonMediaImporter /* = nullptr */)
  : MediaImport(std::make_shared<CMediaImport>(import), addonId, addonMediaImporter)
{
}
#endif

MediaImport::MediaImport(const String& importPath)
  : import(std::make_shared<CMediaImport>(importPath))
{
  if (languageHook != nullptr)
    m_addonId = languageHook->GetAddonId();

  if (m_addonId.empty())
    throw MissingLanguageHookException("MediaImport");

  import->SetRecursive(true);
}

String MediaImport::getPath() const
{
  return import->GetPath();
}

MediaProvider* MediaImport::getProvider() const
{
  return new MediaProvider(std::make_shared<CMediaImportSource>(import->GetSource()), m_addonId,
                           m_addonMediaImporter);
}

void MediaImport::setProvider(const MediaProvider* provider)
{
  if (provider == nullptr)
    return;

  import->SetSource(*provider->source);
}

std::vector<String> MediaImport::getMediaTypes() const
{
  return import->GetMediaTypes();
}

void MediaImport::setMediaTypes(const std::vector<String>& mediaTypes)
{
  import->SetMediaTypes(mediaTypes);
}

String MediaImport::getLastSynced() const
{
  if (!import->GetLastSynced().IsValid())
    return emptyString;

  return import->GetLastSynced().GetAsW3CDateTime(true);
}

XBMCAddon::xbmcaddon::Settings* MediaImport::prepareSettings()
{
  auto settings = import->Settings();
  if (CAddonMediaImporter::PrepareImportSettings(m_addonId, settings))
    return createSettings(settings);

  return nullptr;
}

XBMCAddon::xbmcaddon::Settings* MediaImport::getSettings()
{
  return createSettings(import->Settings());
}

XBMCAddon::xbmcaddon::Settings* MediaImport::createSettings(MediaImportSourceSettingsPtr settings)
{
  if (settings == nullptr)
    return nullptr;

  return new xbmcaddon::Settings(settings, m_addonId, m_addonMediaImporter, &m_callbackData);
}
} // namespace xbmcmediaimport
} // namespace XBMCAddon
