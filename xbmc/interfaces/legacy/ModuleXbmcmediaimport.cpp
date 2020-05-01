/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ModuleXbmcmediaimport.h"

#include "ServiceBroker.h"
#include "interfaces/legacy/LanguageHook.h"
#include "media/import/MediaImportManager.h"
#include "media/import/importers/AddonMediaImporter.h"

namespace XBMCAddon
{
namespace xbmcmediaimport
{
bool addProvider(const MediaProvider* provider)
{
  if (provider == nullptr)
    return false;

  const auto importerId =
      CAddonMediaImporter::GetImporterId(LanguageHook::GetLanguageHook()->GetAddonId());
  provider->source->SetImporterId(importerId);

  return CServiceBroker::GetMediaImportManager().AddSource(*provider->source);
}

bool addAndActivateProvider(const MediaProvider* provider)
{
  if (provider == nullptr)
    return false;

  const auto importerId =
      CAddonMediaImporter::GetImporterId(LanguageHook::GetLanguageHook()->GetAddonId());
  provider->source->SetImporterId(importerId);

  return CServiceBroker::GetMediaImportManager().AddAndActivateSource(*provider->source);
}

bool activateProvider(const MediaProvider* provider)
{
  if (provider == nullptr)
    return false;

  const auto importerId =
      CAddonMediaImporter::GetImporterId(LanguageHook::GetLanguageHook()->GetAddonId());
  provider->source->SetImporterId(importerId);

  return CServiceBroker::GetMediaImportManager().ActivateSource(*provider->source);
}

void deactivateProvider(const String& providerId)
{
  return CServiceBroker::GetMediaImportManager().DeactivateSource(providerId);
}

bool updateProvider(const MediaProvider* provider)
{
  if (provider == nullptr)
    return false;

  CMediaImportManager& mediaImportManager = CServiceBroker::GetMediaImportManager();

  auto updatedSource = provider->source;
  CMediaImportSource source(updatedSource->GetIdentifier());
  if (!mediaImportManager.GetSource(updatedSource->GetIdentifier(), source))
    return false;

  if (updatedSource->GetBasePath().empty())
    source.SetBasePath(updatedSource->GetBasePath());
  if (!updatedSource->GetFriendlyName().empty())
    source.SetFriendlyName(updatedSource->GetFriendlyName());
  if (!updatedSource->GetIconUrl().empty())
    source.SetIconUrl(updatedSource->GetIconUrl());
  if (!updatedSource->GetAvailableMediaTypes().empty())
    source.SetAvailableMediaTypes(updatedSource->GetAvailableMediaTypes());

  return mediaImportManager.UpdateSource(source);
}

void setDiscoveredProvider(int handle, bool providerDiscovered, const MediaProvider* mediaProvider)
{
  MediaImportSourcePtr source;
  if (mediaProvider == nullptr)
    providerDiscovered = false;
  else
    source = mediaProvider->source;

  CAddonMediaImporter::SetDiscoveredProviderDetails(handle, providerDiscovered, source);
}

void setProviderFound(int handle, bool providerFound)
{
  CAddonMediaImporter::SetProviderFound(handle, providerFound);
}

void setCanImport(int handle, bool canImport)
{
  CAddonMediaImporter::SetCanImport(handle, canImport);
}

void setProviderReady(int handle, bool providerReady)
{
  CAddonMediaImporter::SetProviderReady(handle, providerReady);
}

void setImportReady(int handle, bool importReady)
{
  CAddonMediaImporter::SetImportReady(handle, importReady);
}

void setCanUpdateMetadataOnProvider(int handle, bool canUpdateMetadataOnProvider)
{
  CAddonMediaImporter::SetCanUpdateMetadataOnProvider(handle, canUpdateMetadataOnProvider);
}

void setCanUpdatePlaycountOnProvider(int handle, bool canUpdatePlaycountOnProvider)
{
  CAddonMediaImporter::SetCanUpdatePlaycountOnProvider(handle, canUpdatePlaycountOnProvider);
}

void setCanUpdateLastPlayedOnProvider(int handle, bool canUpdateLastPlayedOnProvider)
{
  CAddonMediaImporter::SetCanUpdateLastPlayedOnProvider(handle, canUpdateLastPlayedOnProvider);
}

void setCanUpdateResumePositionOnProvider(int handle, bool canUpdateResumePositionOnProvider)
{
  CAddonMediaImporter::SetCanUpdateResumePositionOnProvider(handle,
                                                            canUpdateResumePositionOnProvider);
}

bool shouldCancel(int handle, unsigned int progress, unsigned int total)
{
  return CAddonMediaImporter::ShouldCancel(handle, progress, total);
}

void setProgressStatus(int handle, const String& status)
{
  CAddonMediaImporter::SetProgressStatus(handle, status);
}

MediaProvider* getProvider(int handle) throw(MediaImportException)
{
  auto mediaImporter = CAddonMediaImporter::GetImporter(handle);
  auto mediaProvider = CAddonMediaImporter::GetMediaProvider(handle);
  if (mediaImporter == nullptr || mediaProvider == nullptr)
    throw MediaImportException("Invalid media provider handle");

  return new MediaProvider(mediaProvider, mediaImporter->GetAddonId(), mediaImporter);
}

MediaImport* getImport(int handle) throw(MediaImportException)
{
  auto mediaImporter = CAddonMediaImporter::GetImporter(handle);
  auto mediaImport = CAddonMediaImporter::GetMediaImport(handle);
  if (mediaImporter == nullptr || mediaImport == nullptr)
    throw MediaImportException("Invalid media import handle");

  return new MediaImport(mediaImport, mediaImporter->GetAddonId(), mediaImporter);
}

std::vector<XBMCAddon::xbmcgui::ListItem*> getImportedItems(int handle, const String& mediaType)
{
  const auto items = CAddonMediaImporter::GetImportedItems(handle, mediaType);

  std::vector<XBMCAddon::xbmcgui::ListItem*> importedItems;
  for (const auto& item : items)
    importedItems.push_back(new XBMCAddon::xbmcgui::ListItem(item));

  return importedItems;
}

void addImportItem(int handle,
                   const XBMCAddon::xbmcgui::ListItem* item,
                   const String& mediaType,
                   int changesetType /* = MediaImportChangesetTypeNone */)
{
  AddonClass::Ref<xbmcgui::ListItem> pItem(item);

  CAddonMediaImporter::AddImportItem(handle, pItem->item, mediaType,
                                     static_cast<MediaImportChangesetType>(changesetType));
}

void addImportItems(int handle,
                    const std::vector<XBMCAddon::xbmcgui::ListItem*>& items,
                    const String& mediaType,
                    int changesetType /* = MediaImportChangesetTypeNone */)
{
  std::vector<CFileItemPtr> fileItems;
  for (const auto& item : items)
    fileItems.push_back(item->item);

  CAddonMediaImporter::AddImportItems(handle, fileItems, mediaType,
                                      static_cast<MediaImportChangesetType>(changesetType));
}

void finishImport(int handle, bool isChangeset /* = false */)
{
  CAddonMediaImporter::FinishImport(handle, isChangeset);
}

XBMCAddon::xbmcgui::ListItem* getUpdatedItem(int handle) throw(MediaImportException)
{
  auto item = CAddonMediaImporter::GetUpdatedItem(handle);
  return new XBMCAddon::xbmcgui::ListItem(item);
}

void finishUpdateOnProvider(int handle)
{
  CAddonMediaImporter::FinishUpdateOnProvider(handle);
}

bool changeImportedItems(const MediaImport* import,
                         const std::vector<ChangesetListItem>& changedItems)
{
  if (import == nullptr || import->import == nullptr)
    return false;

  ChangesetItems changesetItems;
  for (const auto& changedItem : changedItems)
  {
    AddonClass::Ref<XBMCAddon::xbmcgui::ListItem> pChangedItem(changedItem.second());

    changesetItems.emplace_back(static_cast<MediaImportChangesetType>(changedItem.first()),
                                pChangedItem->item);
  }

  return CServiceBroker::GetMediaImportManager().ChangeImportedItems(*import->import,
                                                                     changesetItems);
}

MediaProvider* getProviderById(const String& providerId)
{
  if (providerId.empty())
    return nullptr;

  CMediaImportSource mediaProvider(providerId);
  if (!CServiceBroker::GetMediaImportManager().GetSource(providerId, mediaProvider))
    return nullptr;

  return new MediaProvider(mediaProvider);
}

std::vector<XBMCAddon::xbmcgui::ListItem*> getImportedItemsByProvider(const MediaProvider* provider)
{
  if (provider == nullptr || provider->source == nullptr)
    return {};

  const auto items =
      CServiceBroker::GetMediaImportManager().GetImportedItemsBySource(*provider->source);
  std::vector<XBMCAddon::xbmcgui::ListItem*> importedItems;
  for (const auto& item : items)
    importedItems.push_back(new XBMCAddon::xbmcgui::ListItem(item));

  return importedItems;
}

std::vector<XBMCAddon::xbmcgui::ListItem*> getImportedItemsByImport(const MediaImport* import)
{
  if (import == nullptr || import->import == nullptr)
    return {};

  const auto items =
      CServiceBroker::GetMediaImportManager().GetImportedItemsByImport(*import->import);
  std::vector<XBMCAddon::xbmcgui::ListItem*> importedItems;
  for (const auto& item : items)
    importedItems.push_back(new XBMCAddon::xbmcgui::ListItem(item));

  return importedItems;
}
} // namespace xbmcmediaimport
} // namespace XBMCAddon
