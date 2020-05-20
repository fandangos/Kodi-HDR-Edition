/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoImportHandler.h"

#include "FileItem.h"
#include "ServiceBroker.h"
#include "interfaces/AnnouncementManager.h"
#include "media/import/MediaImport.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"
#include "video/VideoDatabase.h"

#include <algorithm>

CVideoImportHandler::CVideoImportHandler(const IMediaImportHandlerManager* importHandlerManager)
  : IMediaImportHandler(importHandlerManager), CStaticLoggerBase("CVideoImportHandler")
{
}

std::string CVideoImportHandler::GetItemLabel(const CFileItem* item) const
{
  if (item == nullptr)
    return "";

  if (item->HasVideoInfoTag() && !item->GetVideoInfoTag()->m_strTitle.empty())
    return item->GetVideoInfoTag()->m_strTitle;

  return item->GetLabel();
}

bool CVideoImportHandler::GetLocalItems(const CMediaImport& import,
                                        std::vector<CFileItemPtr>& items) const
{
  if (!m_db.Open())
    return false;

  bool result = GetLocalItems(m_db, import, items);

  m_db.Close();
  return result;
}

bool CVideoImportHandler::StartChangeset(const CMediaImport& import)
{
  // start the background loader if necessary
  if (import.Settings()->UpdateImportedMediaItems())
    m_thumbLoader.OnLoaderStart();

  return true;
}

bool CVideoImportHandler::FinishChangeset(const CMediaImport& import)
{
  // stop the background loader if necessary
  if (import.Settings()->UpdateImportedMediaItems())
    m_thumbLoader.OnLoaderFinish();

  return true;
}

CFileItemPtr CVideoImportHandler::FindMatchingLocalItem(
    const CMediaImport& import,
    const CFileItem* item,
    const std::vector<CFileItemPtr>& localItems) const
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return nullptr;

  const auto& localItem =
      std::find_if(localItems.cbegin(), localItems.cend(), [item](const CFileItemPtr localItem) {
        return localItem->HasVideoInfoTag() &&
               localItem->GetVideoInfoTag()->GetPath() == item->GetVideoInfoTag()->GetPath();
      });
  if (localItem != localItems.cend())
    return *localItem;

  return nullptr;
}

MediaImportChangesetType CVideoImportHandler::DetermineChangeset(const CMediaImport& import,
                                                                 const CFileItem* item,
                                                                 const CFileItemPtr& localItem)
{
  if (item == nullptr || localItem == nullptr || !item->HasVideoInfoTag() ||
      !localItem->HasVideoInfoTag())
    return MediaImportChangesetType::None;

  const auto& settings = import.Settings();

  // retrieve all details for the previously imported item
  if (!m_thumbLoader.LoadItem(localItem.get()))
  {
    s_logger->warn("failed to retrieve details for local item {} during media importing",
                   localItem->GetVideoInfoTag()->GetPath());
  }

  // compare the previously imported item with the newly imported item
  if (Compare(localItem.get(), item, settings->UpdateImportedMediaItems(),
              settings->UpdatePlaybackMetadataFromSource()))
    return MediaImportChangesetType::None;

  return MediaImportChangesetType::Changed;
}

void CVideoImportHandler::PrepareImportedItem(const CMediaImport& import,
                                              CFileItem* item,
                                              const CFileItemPtr& localItem) const
{
  if (item == nullptr || localItem == nullptr || !localItem->HasVideoInfoTag() ||
      !localItem->HasVideoInfoTag())
    return;

  auto itemVideoInfoTag = item->GetVideoInfoTag();
  const auto localItemVideoInfoTag = localItem->GetVideoInfoTag();

  itemVideoInfoTag->m_iDbId = localItemVideoInfoTag->m_iDbId;
  itemVideoInfoTag->m_iFileId = localItemVideoInfoTag->m_iFileId;
  itemVideoInfoTag->m_iIdShow = localItemVideoInfoTag->m_iIdShow;
  itemVideoInfoTag->m_iIdSeason = localItemVideoInfoTag->m_iIdSeason;

  item->SetSource(localItem->GetSource());
  item->SetImportPath(localItem->GetImportPath());
  itemVideoInfoTag->m_basePath = localItemVideoInfoTag->m_basePath;
  itemVideoInfoTag->m_parentPathID = localItemVideoInfoTag->m_parentPathID;
}

bool CVideoImportHandler::StartSynchronisation(const CMediaImport& import)
{
  m_sourcePaths.clear();
  m_importPathIds.clear();

  if (!m_db.Open())
    return false;

  // TODO(Montellese): is a transaction really needed?
  m_db.BeginTransaction();

  CServiceBroker::GetAnnouncementManager()->Announce(ANNOUNCEMENT::VideoLibrary, "xbmc",
                                                     "OnScanStarted");
  return true;
}

bool CVideoImportHandler::FinishSynchronisation(const CMediaImport& import)
{
  if (!m_db.IsOpen())
    return false;

  // now make sure the items are enabled
  SetImportedItemsEnabled(import, true);

  // TODO(Montellese): is a transaction really needed?
  m_db.CommitTransaction();
  m_db.Close();

  m_sourcePaths.clear();
  m_importPathIds.clear();

  CServiceBroker::GetAnnouncementManager()->Announce(ANNOUNCEMENT::VideoLibrary, "xbmc",
                                                     "OnScanFinished");

  return true;
}

bool CVideoImportHandler::RemoveImportedItems(const CMediaImport& import)
{
  if (!m_db.Open())
    return false;

  m_db.BeginTransaction();

  bool success = RemoveImportedItems(m_db, import);

  if (success)
    m_db.CommitTransaction();
  else
    m_db.RollbackTransaction();

  m_db.Close();
  return success;
}

void CVideoImportHandler::SetImportedItemsEnabled(const CMediaImport& import, bool enable)
{
  if (!m_db.Open())
    return;

  m_db.SetImportItemsEnabled(enable, GetMediaType(), import);
  m_db.Close();
}

bool CVideoImportHandler::RemoveImportedItems(CVideoDatabase& videodb,
                                              const CMediaImport& import) const
{
  return videodb.DeleteItemsFromImport(import);
}

void CVideoImportHandler::PrepareItem(const CMediaImport& import, CFileItem* pItem)
{
  if (pItem == nullptr || !pItem->HasVideoInfoTag() || import.GetPath().empty() ||
      import.GetSource().GetIdentifier().empty())
    return;

  const std::string& sourcePath = import.GetSource().GetBasePath();
  const std::string& importPath = import.GetPath();

  // only add the source path to the database if it isn't already known
  if (m_sourcePaths.find(sourcePath) == m_sourcePaths.end())
  {
    m_db.AddPath(sourcePath);
    m_sourcePaths.insert(sourcePath);
  }

  // only add the import path to the database if it isn't already known
  int idPath = -1;
  const auto& importPathId = m_importPathIds.find(importPath);
  if (importPathId == m_importPathIds.end())
  {
    idPath = m_db.AddPath(importPath, sourcePath);
    m_importPathIds.emplace(importPath, idPath);
  }
  else
    idPath = importPathId->second;

  // set the proper source
  pItem->SetSource(import.GetSource().GetIdentifier());
  pItem->SetImportPath(importPath);

  auto videoInfoTag = pItem->GetVideoInfoTag();

  // set the proper base and parent path
  videoInfoTag->m_basePath = sourcePath;
  videoInfoTag->m_parentPathID = idPath;

  if (!pItem->m_bIsFolder)
    videoInfoTag->m_iFileId = m_db.AddFile(
        pItem->GetPath(), importPath, videoInfoTag->GetPlayCount(), videoInfoTag->m_lastPlayed);
}

void CVideoImportHandler::SetDetailsForFile(const CFileItem* pItem, bool reset)
{
  const auto videoInfoTag = pItem->GetVideoInfoTag();

  // update playcount and lastplayed
  m_db.SetPlayCount(*pItem, videoInfoTag->GetPlayCount(), videoInfoTag->m_lastPlayed, false);

  // clean resume bookmark
  if (reset)
    m_db.DeleteResumeBookMark(*pItem, false);

  if (videoInfoTag->GetResumePoint().IsPartWay())
    m_db.AddBookMarkToFile(pItem->GetPath(), videoInfoTag->GetResumePoint(), CBookmark::RESUME);
}

bool CVideoImportHandler::SetImportForItem(const CFileItem* pItem, const CMediaImport& import)
{
  return m_db.SetImportForItem(pItem->GetVideoInfoTag()->m_iDbId, GetMediaType(), import);
}

void CVideoImportHandler::RemoveFile(CVideoDatabase& videodb, const CFileItem* item) const
{
  if (!videodb.IsOpen() || item == nullptr || !item->HasVideoInfoTag())
    return;

  videodb.DeleteFile(item->GetVideoInfoTag()->m_iFileId, item->GetVideoInfoTag()->GetPath());
}

bool CVideoImportHandler::Compare(const CFileItem* originalItem,
                                  const CFileItem* newItem,
                                  bool allMetadata /* = true */,
                                  bool playbackMetadata /* = true */) const
{
  if (originalItem == nullptr || !originalItem->HasVideoInfoTag() || newItem == nullptr ||
      !newItem->HasVideoInfoTag())
    return false;

  const auto originalItemVideoInfoTag = originalItem->GetVideoInfoTag();
  const auto newItemVideoInfoTag = newItem->GetVideoInfoTag();

  if (!allMetadata)
  {
    return originalItemVideoInfoTag->GetPlayCount() == newItemVideoInfoTag->GetPlayCount() &&
           originalItemVideoInfoTag->m_lastPlayed == newItemVideoInfoTag->m_lastPlayed &&
           originalItemVideoInfoTag->GetResumePoint().timeInSeconds ==
               newItemVideoInfoTag->GetResumePoint().timeInSeconds;
  }

  auto originalArt = originalItem->GetArt();
  const auto& newArt = newItem->GetArt();
  if (originalArt != newArt)
  {
    // if the number of artwork is identical something must have changed in the URLs
    if (originalArt.size() == newArt.size())
      return false;

    // remove any artwork Kodi automatically adds
    std::set<std::string> parentPrefixes;
    if (originalItemVideoInfoTag->m_type == MediaTypeMovie)
      parentPrefixes = {"set"};
    else if (originalItemVideoInfoTag->m_type == MediaTypeSeason ||
             originalItemVideoInfoTag->m_type == MediaTypeEpisode)
      parentPrefixes = {"tvshow", "season"};
    RemoveAutoArtwork(originalArt, parentPrefixes);

    if (originalArt != newArt)
      return false;
  }

  if (originalItemVideoInfoTag->Equals(*newItemVideoInfoTag, true))
    return true;

  std::set<Field> differences;
  if (!originalItemVideoInfoTag->GetDifferences(*newItemVideoInfoTag, differences, true))
    return true;

  // if playback metadata shouldn't be compared simply remove them from the list of differences
  if (!playbackMetadata)
  {
    differences.erase(FieldPlaycount); // playcount
    differences.erase(FieldLastPlayed); // lastplayed
    differences.erase(FieldInProgress); // resume point
  }

  // check and remove any media type specific ignored properties
  const auto ignoredDifferences = IgnoreDifferences();
  for (const auto& difference : ignoredDifferences)
    differences.erase(difference);

  // special handling for actors without artwork
  const auto& it = differences.find(FieldActor);
  if (it != differences.end())
  {
    bool equal = false;
    const std::vector<SActorInfo>& originalCast = originalItemVideoInfoTag->m_cast;
    const std::vector<SActorInfo>& newCast = newItemVideoInfoTag->m_cast;
    // ignore differences in cast if the imported item doesn't provide a cast at all
    if (newCast.empty())
      equal = true;
    else if (originalCast.size() == newCast.size())
    {
      equal = true;
      for (size_t index = 0; index < originalCast.size(); ++index)
      {
        const SActorInfo& originalActor = originalCast.at(index);
        const SActorInfo& newActor = newCast.at(index);

        if (originalActor.strName != newActor.strName ||
            originalActor.strRole != newActor.strRole ||
            (!newActor.thumb.empty() && originalActor.thumb != newActor.thumb) ||
            (!newActor.thumbUrl.m_xml.empty() &&
             originalActor.thumbUrl.m_xml != newActor.thumbUrl.m_xml))
        {
          equal = false;
          break;
        }
      }
    }

    if (equal)
      differences.erase(it);
  }

  if (!differences.empty())
    return false;

  return true;
}

void CVideoImportHandler::RemoveAutoArtwork(CGUIListItem::ArtMap& artwork,
                                            const std::set<std::string>& parentPrefixes)
{
  for (auto art = artwork.begin(); art != artwork.end();)
  {
    bool remove = false;
    // check for default artwork
    if (art->second == "DefaultVideo.png" || art->second == "DefaultFolder.png")
      remove = true;
    // check for image:// artwork
    else if (StringUtils::StartsWith(art->second, "image://"))
      remove = true;
    // check for parent artwork
    else if (!parentPrefixes.empty() &&
             std::any_of(parentPrefixes.begin(), parentPrefixes.end(),
                         [art](const std::string& parentPrefix) {
                           return StringUtils::StartsWith(art->first, parentPrefix + ".");
                         }))
      remove = true;

    if (remove)
      art = artwork.erase(art);
    else
      ++art;
  }
}
