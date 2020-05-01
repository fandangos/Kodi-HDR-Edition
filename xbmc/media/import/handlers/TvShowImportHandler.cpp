/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TvShowImportHandler.h"

#include "FileItem.h"
#include "media/import/MediaImport.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "video/VideoDatabase.h"

#include <algorithm>

/*!
 * Checks whether two tvshows are the same by comparing them by title and year
 */
static bool IsSameTVShow(const CVideoInfoTag& left, const CVideoInfoTag& right)
{
  return left.m_strTitle == right.m_strTitle && left.GetYear() == right.GetYear();
}

CFileItemPtr CTvShowImportHandler::FindMatchingLocalItem(
    const CMediaImport& import,
    const CFileItem* item,
    const std::vector<CFileItemPtr>& localItems) const
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return nullptr;

  const auto& localItem =
      std::find_if(localItems.cbegin(), localItems.cend(), [&item](const CFileItemPtr& localItem) {
        return IsSameTVShow(*item->GetVideoInfoTag(), *localItem->GetVideoInfoTag());
      });

  if (localItem != localItems.cend())
    return *localItem;

  return nullptr;
}

bool CTvShowImportHandler::AddImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr)
    return false;

  // make sure that the source and import path are set
  PrepareItem(import, item);

  // and prepare the tvshow paths
  std::vector<std::pair<std::string, std::string>> tvshowPaths;
  tvshowPaths.push_back(std::make_pair(item->GetPath(), item->GetVideoInfoTag()->m_basePath));
  // we don't know the season art yet
  std::map<int, std::map<std::string, std::string>> seasonArt;

  const CVideoInfoTag* info = item->GetVideoInfoTag();

  // check if there already is a local tvshow with the same name
  CFileItemList tvshows;
  m_db.GetTvShowsByName(info->m_strTitle, tvshows);
  bool exists = false;
  if (!tvshows.IsEmpty())
  {
    CFileItemPtr tvshow;
    for (int i = 0; i < tvshows.Size();)
    {
      tvshow = tvshows.Get(i);
      // remove tvshows without a CVideoInfoTag
      if (!tvshow->HasVideoInfoTag())
      {
        tvshows.Remove(i);
        continue;
      }

      CVideoInfoTag* tvshowInfo = tvshow->GetVideoInfoTag();
      if (!m_db.GetTvShowInfo(tvshowInfo->GetPath(), *tvshowInfo, tvshowInfo->m_iDbId,
                              tvshow.get()))
      {
        tvshows.Remove(i);
        continue;
      }

      // check if the scraper identifier or the title and year match
      if ((tvshowInfo->HasUniqueID() && tvshowInfo->GetUniqueID() == info->GetUniqueID()) ||
          (tvshowInfo->HasYear() && tvshowInfo->GetYear() == info->GetYear() &&
           tvshowInfo->m_strTitle == info->m_strTitle))
      {
        exists = true;
        break;
      }
      // remove tvshows that don't even match in title
      else if (tvshowInfo->m_strTitle != info->m_strTitle)
      {
        tvshows.Remove(i);
        continue;
      }

      ++i;
    }

    // if there was no exact match and there are still tvshows left that match in title
    // and the new item doesn't have a scraper identifier and no year
    // we take the first match
    if (!exists && !tvshows.IsEmpty() && !info->HasUniqueID() && !info->HasYear())
    {
      tvshow = tvshows.Get(0);
      exists = true;
    }

    // simply add the path of the imported tvshow to the tvshow's paths
    if (exists && tvshow != nullptr)
      item->GetVideoInfoTag()->m_iDbId =
          m_db.SetDetailsForTvShow(tvshowPaths, *(tvshow->GetVideoInfoTag()), tvshow->GetArt(),
                                   seasonArt, tvshow->GetVideoInfoTag()->m_iDbId);
  }

  // couldn't find a matching local tvshow so add the newly imported one
  if (!exists)
    item->GetVideoInfoTag()->m_iDbId = m_db.SetDetailsForTvShow(
        tvshowPaths, *(item->GetVideoInfoTag()), item->GetArt(), seasonArt);

  // make sure that the tvshow was properly added
  if (item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  return SetImportForItem(item, import);
}

bool CTvShowImportHandler::UpdateImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  std::vector<std::pair<std::string, std::string>> tvshowPaths;
  tvshowPaths.push_back(std::make_pair(item->GetPath(), item->GetVideoInfoTag()->m_basePath));
  std::map<int, std::map<std::string, std::string>> seasonArt;
  return m_db.SetDetailsForTvShow(tvshowPaths, *(item->GetVideoInfoTag()), item->GetArt(),
                                  seasonArt, item->GetVideoInfoTag()->m_iDbId) > 0;
}

bool CTvShowImportHandler::RemoveImportedItem(const CMediaImport& import, const CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  // get all paths belonging to the tvshow
  std::map<int, std::string> tvshowPaths;
  if (!m_db.GetPathsForTvShow(item->GetVideoInfoTag()->m_iDbId, tvshowPaths))
    return false;

  // something is wrong as the tvshow doesn't have any paths
  if (tvshowPaths.empty())
    return false;

  // we only handle the case where more than one path belongs to the tvshow because
  // we can't delete the tvshow completely before not having synced the episodes
  if (tvshowPaths.size() == 1)
    return true;

  for (const auto& tvshowPath : tvshowPaths)
  {
    // check if the tvshow path is a sub-path of the media import
    if (URIUtils::PathHasParent(tvshowPath.second, import.GetPath().c_str()))
    {
      // remove the path from the tvshow
      m_db.RemovePathFromTvShow(item->GetVideoInfoTag()->m_iDbId, tvshowPath.first);
      m_db.RemoveImportFromItem(item->GetVideoInfoTag()->m_iDbId, GetMediaType(), import);
      break;
    }
  }

  return true;
}

bool CTvShowImportHandler::CleanupImportedItems(const CMediaImport& import)
{
  if (!m_db.Open())
    return false;

  std::vector<CFileItemPtr> importedTvShows;
  if (!GetLocalItems(m_db, import, importedTvShows))
    return false;

  m_db.BeginTransaction();

  for (const auto& importedTvShow : importedTvShows)
  {
    if (!importedTvShow->HasVideoInfoTag() || importedTvShow->GetVideoInfoTag()->m_iDbId <= 0)
      continue;

    // get all episodes of the tvshow
    CVideoDbUrl videoUrl;
    if (!videoUrl.FromString(StringUtils::Format("videodb://tvshows/titles/{}/-1/",
                                                 importedTvShow->GetVideoInfoTag()->m_iDbId)))
      continue;
    videoUrl.AddOption("tvshowid", importedTvShow->GetVideoInfoTag()->m_iDbId);

    CFileItemList episodes;
    if (!m_db.GetEpisodesByWhere(videoUrl.ToString(), CDatabase::Filter(), episodes, true,
                                 SortDescription(), false))
      continue;

    // loop through all episodes and count the imported ones
    bool hasImportedEpisodes = false;
    for (int i = 0; i < episodes.Size(); ++i)
    {
      if (episodes.Get(i)->IsImported())
      {
        hasImportedEpisodes = true;
        break;
      }
    }

    // if there are no imported episodes we can remove the tvshow
    if (!hasImportedEpisodes)
      RemoveImportedItem(m_db, import, importedTvShow.get());
  }

  m_db.CommitTransaction();

  return true;
}

bool CTvShowImportHandler::GetLocalItems(CVideoDatabase& videodb,
                                         const CMediaImport& import,
                                         std::vector<CFileItemPtr>& items) const
{
  CFileItemList tvshows;
  if (!videodb.GetTvShowsByWhere(
          "videodb://tvshows/titles/?imported&import=" + CURL::Encode(import.GetPath()),
          CDatabase::Filter(), tvshows, SortDescription(),
          import.Settings()->UpdateImportedMediaItems() ? VideoDbDetailsAll : VideoDbDetailsNone))
    return false;

  items.insert(items.begin(), tvshows.cbegin(), tvshows.cend());

  return true;
}

std::set<Field> CTvShowImportHandler::IgnoreDifferences() const
{
  return {FieldAlbum,         FieldArtist,
          FieldCountry,       FieldDirector,
          FieldEpisodeNumber, FieldEpisodeNumberSpecialSort,
          FieldFilename,      FieldInProgress,
          FieldLastPlayed,    FieldPlaycount,
          FieldPlotOutline,   FieldProductionCode,
          FieldSeason,        FieldSeasonSpecialSort,
          FieldSet,           FieldTagline,
          FieldTime,          FieldTop250,
          FieldTrackNumber,   FieldTvShowTitle,
          FieldWriter};
}

bool CTvShowImportHandler::RemoveImportedItems(CVideoDatabase& videodb,
                                               const CMediaImport& import) const
{
  std::vector<CFileItemPtr> items;
  if (!GetLocalItems(videodb, import, items))
    return false;

  for (const auto& item : items)
    RemoveImportedItem(videodb, import, item.get());

  return true;
}

void CTvShowImportHandler::RemoveImportedItem(CVideoDatabase& videodb,
                                              const CMediaImport& import,
                                              const CFileItem* item) const
{
  // check if the tvshow still has episodes or not
  if (item == nullptr || !item->HasVideoInfoTag())
    return;

  // if there are other episodes only remove the path and the import link to the tvshow and not the whole tvshow
  if (item->GetVideoInfoTag()->m_iEpisode > 0)
  {
    videodb.RemovePathFromTvShow(item->GetVideoInfoTag()->m_iDbId,
                                 item->GetVideoInfoTag()->GetPath());
    videodb.RemoveImportFromItem(item->GetVideoInfoTag()->m_iDbId, GetMediaType(), import);
  }
  else
    videodb.DeleteTvShow(item->GetVideoInfoTag()->m_iDbId, false, false);

  // either way remove the path
  videodb.DeletePath(-1, item->GetVideoInfoTag()->m_strPath);
}
