/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "UPnPMediaImporter.h"

#include "FileItem.h"
#include "ServiceBroker.h"
#include "URL.h"
#include "dialogs/GUIDialogExtendedProgressBar.h"
#include "guilib/LocalizeStrings.h"
#include "media/import/jobs/tasks/MediaImportImportItemsRetrievalTask.h"
#include "media/import/jobs/tasks/MediaImportUpdateTask.h"
#include "network/upnp/UPnP.h"
#include "network/upnp/UPnPInternal.h"
#include "settings/Settings.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"
#include "video/VideoInfoTag.h"

#include <Platinum/Source/Platinum/Platinum.h>
#include <fmt/ostream.h>

using namespace UPNP;

#define UPNP_ROOT_CONTAINER_ID "0"

typedef struct {
  MediaType mediaType;
  const char* objectIdentification;
} SupportedMediaType;

static SupportedMediaType SupportedMediaTypes[] = {
  { MediaTypeMovie,       "object.item.videoItem.movie" },
  { MediaTypeTvShow,      "object.container.album.videoAlbum.videoBroadcastShow" },
  { MediaTypeSeason,      "object.container.album.videoAlbum.videoBroadcastSeason" },
  { MediaTypeEpisode,     "object.item.videoItem.videoBroadcast" },
  { MediaTypeMusicVideo,  "object.item.videoItem.musicVideoClip" },
  { MediaTypeArtist,      "object.container.person.musicArtist" },
  { MediaTypeAlbum,       "object.container.album.musicAlbum" },
  { MediaTypeSong,        "object.item.audioItem" }
};

#define SupportedMediaTypesSize sizeof(SupportedMediaTypes) / sizeof(SupportedMediaType)

static bool FindServer(const std::string &deviceUUID, PLT_DeviceDataReference &device)
{
  if (deviceUUID.empty())
    return false;

  return CUPnP::GetInstance()->m_MediaBrowser->FindServer(deviceUUID.c_str(), device) == NPT_SUCCESS;
}

static CFileItemPtr ConstructItem(PLT_DeviceDataReference &device, PLT_MediaObject *object)
{
  CFileItemPtr pItem = BuildObject(object);
  std::string id;
  if (object->m_ReferenceID.IsEmpty())
    id = (const char*)object->m_ObjectID;
  else
    id = (const char*)object->m_ReferenceID;

  CURL::Encode(id);
  URIUtils::AddSlashAtEnd(id);
  pItem->SetPath("upnp://" + std::string(device->GetUUID()) + "/" + id);
  pItem->GetVideoInfoTag()->m_strPath = pItem->GetVideoInfoTag()->m_strFileNameAndPath = pItem->GetPath();
  
  return pItem;
}

static void ConstructList(PLT_DeviceDataReference &device, PLT_MediaObjectListReference &list, std::vector<CFileItemPtr> &items)
{
  if (list.IsNull())
    return;

  for (PLT_MediaObjectList::Iterator entry = list->GetFirstItem() ; entry ; ++entry)
  {
    CFileItemPtr item(ConstructItem(device, *entry));
    if (item != NULL)
      items.push_back(item);
  }
}

static bool Search(PLT_DeviceDataReference &device, const std::string &search_criteria, std::vector<CFileItemPtr> &items)
{
  PLT_SyncMediaBrowser *mediaBrowser = CUPnP::GetInstance()->m_MediaBrowser;
  if (mediaBrowser == NULL)
    return false;

  PLT_MediaObjectListReference list;
  if (mediaBrowser->SearchSync(device, UPNP_ROOT_CONTAINER_ID, search_criteria.c_str(), list) != NPT_SUCCESS)
    return false;

  ConstructList(device, list, items);
  return true;
}

static bool Search(CMediaImportImportItemsRetrievalTask *task, PLT_DeviceDataReference &device, const MediaType &mediaType, std::vector<CFileItemPtr> &items)
{
  SupportedMediaType* supportedMediaType = NULL;
  for (size_t i = 0; i < SupportedMediaTypesSize; i++)
  {
    if (mediaType == SupportedMediaTypes[i].mediaType)
    {
      supportedMediaType = &SupportedMediaTypes[i];
      break;
    }
  }

  if (supportedMediaType == NULL)
    return false;

  task->SetProgressText(StringUtils::Format(g_localizeStrings.Get(39568).c_str(), CMediaTypes::GetPluralLocalization(supportedMediaType->mediaType).c_str()));
  if (!Search(device, StringUtils::Format("upnp:class = \"{}\"", supportedMediaType->objectIdentification), items))
    return false;

  if (mediaType == MediaTypeTvShow || mediaType == MediaTypeSeason)
  {
    std::vector<CFileItemPtr> showItems;
    for (std::vector<CFileItemPtr>::iterator it = items.begin() ; it != items.end() ; ++it)
    { // discard video albums that are NOT tv shows
      if ((*it)->HasVideoInfoTag() && (*it)->GetVideoInfoTag()->m_type == mediaType)
        showItems.push_back(*it);
    }

    items.clear();
    items.assign(showItems.begin(), showItems.end());
  }

  return true;
}

constexpr char CUPnPMediaImporterBase::IDENTIFICATION[];

CUPnPMediaImporter::CUPnPMediaImporter()
  : m_logger(CServiceBroker::GetLogging().GetLogger("CUPnPMediaImporter"))
{
}

bool CUPnPMediaImporter::CanImport(const std::string& path)
{
  PLT_DeviceDataReference device;
  if (!validatePath(path, device))
    return false;

  PLT_SyncMediaBrowser *mediaBrowser = CUPnP::GetInstance()->m_MediaBrowser;
  if (mediaBrowser == nullptr)
    return false;

  NPT_String searchCapabilities;
  if (NPT_FAILED(mediaBrowser->GetSearchCapabilitiesSync(device, searchCapabilities)))
    return false;

  return !searchCapabilities.IsEmpty() && searchCapabilities.Find("upnp:class") >= 0;
}

bool CUPnPMediaImporter::IsSourceReady(CMediaImportSource& source)
{
  return CanImport(source.GetBasePath());
}

bool CUPnPMediaImporter::IsImportReady(CMediaImport& import)
{
  return CanImport(import.GetPath());
}

bool CUPnPMediaImporter::CanUpdatePlaycountOnSource(const std::string& path)
{
  // might be too restrictive
  return isXbmcServer(path);
}

bool CUPnPMediaImporter::CanUpdateLastPlayedOnSource(const std::string& path)
{
  // might be too restrictive
  return isXbmcServer(path);
}

bool CUPnPMediaImporter::CanUpdateResumePositionOnSource(const std::string& path)
{
  // might be too restrictive
  return isXbmcServer(path);
}

bool CUPnPMediaImporter::Import(CMediaImportImportItemsRetrievalTask* task)
{
  if (task == NULL)
    return false;

  const auto& import = task->GetImport();

  std::string deviceUUID;
  if (!getDeviceIdentifier(import.GetPath(), deviceUUID))
  {
    m_logger->warn("unable to import media items from unknown path {}", import.GetPath());
    return false;
  }

  PLT_DeviceDataReference device;
  if (!FindServer(deviceUUID, device))
  {
    m_logger->warn("unable to import media items from unavailable source {}", deviceUUID);
    return false;
  }

  for (const auto& importedMediaType : import.GetMediaTypes())
  {
    bool success = false;
    for (size_t i = 0; i < SupportedMediaTypesSize; i++)
    {
      if (CMediaTypes::IsMediaType(importedMediaType, SupportedMediaTypes[i].mediaType))
      {
        success = true;
        break;
      }
    }

    if (!success)
      return false;

    success = false;
    if (task->ShouldCancel(0, 1))
      return false;

    std::vector<CFileItemPtr> items;
    if (!Search(task, device, importedMediaType, items))
      return false;

    // ignore any items that are not part of the requested path
    std::vector<CFileItemPtr> itemList;
    for (auto& item : items)
    {
      if (URIUtils::PathHasParent(item->GetPath(), import.GetPath()))
        itemList.push_back(item);
    }

    task->AddItems(itemList, importedMediaType, MediaImportChangesetType::None);
  }
  return true;
}

bool CUPnPMediaImporter::UpdateOnSource(CMediaImportUpdateTask* task)
{
  if (task == NULL || !task->GetItem().IsImported())
    return false;

  const auto& import = task->GetImport();

  std::string deviceUUID;
  if (!getDeviceIdentifier(import.GetPath(), deviceUUID))
  {
    m_logger->warn("unable to update imported media item on unknown source {}", import.GetSource());
    return false;
  }

  PLT_DeviceDataReference device;
  if (!FindServer(deviceUUID, device))
  {
    m_logger->warn("unable to update imported media item on unavailable source {}", deviceUUID);
    return false;
  }

  const auto& importSettings = import.Settings();
  if (!importSettings->UpdatePlaybackMetadataOnSource())
    return false;
  
  const std::string& importPath = import.GetPath();
  if (!CanUpdatePlaycountOnSource(importPath) &&
      !CanUpdateLastPlayedOnSource(importPath) &&
      !CanUpdateResumePositionOnSource(importPath))
    return false;

  const CFileItem& item = task->GetItem();
  std::string url;
  if (item.HasVideoInfoTag())
    url = item.GetVideoInfoTag()->GetPath();

  if (url.empty())
    return false;

  return CUPnP::GetInstance()->UpdateItem(url, task->GetItem());
}

bool CUPnPMediaImporter::getDeviceIdentifier(const std::string& path, std::string& deviceIdentifier)
{
  if (path.empty() || !URIUtils::IsUPnP(path))
    return false;

  CURL url(path);
  deviceIdentifier = url.GetHostName();

  return true;
}

bool CUPnPMediaImporter::validatePath(const std::string& path, PLT_DeviceDataReference &device)
{
  std::string deviceUUID;
  if (!getDeviceIdentifier(path, deviceUUID))
    return false;

  return FindServer(deviceUUID, device);
}

bool CUPnPMediaImporter::isXbmcServer(const std::string &path)
{
  PLT_DeviceDataReference device;
  if (!validatePath(path, device))
    return false;

  PLT_SyncMediaBrowser *mediaBrowser = CUPnP::GetInstance()->m_MediaBrowser;
  if (mediaBrowser == NULL)
    return false;

  NPT_String sortCapabilities;
  if (NPT_FAILED(mediaBrowser->GetSortCapabilitiesSync(device, sortCapabilities)))
    return false;

  return !sortCapabilities.IsEmpty() && sortCapabilities.Find("xbmc:") >= 0;
}
