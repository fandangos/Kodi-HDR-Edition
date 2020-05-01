/*MovieSetImportHandler
 *  Copyright (C) 2020 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MovieSetImportHandler.h"

#include "FileItem.h"
#include "media/import/MediaImport.h"
#include "video/VideoDatabase.h"

CFileItemPtr CMovieSetImportHandler::FindMatchingLocalItem(
    const CMediaImport& import,
    const CFileItem* item,
    const std::vector<CFileItemPtr>& localItems) const
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return nullptr;

  const auto& localItem =
      std::find_if(localItems.cbegin(), localItems.cend(), [&item](const CFileItemPtr& localItem) {
        return item->GetVideoInfoTag()->m_strTitle == localItem->GetVideoInfoTag()->m_strTitle;
      });

  if (localItem != localItems.cend())
    return *localItem;

  return nullptr;
}

bool CMovieSetImportHandler::AddImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr)
    return false;

  PrepareItem(import, item);

  item->GetVideoInfoTag()->m_iDbId =
      m_db.SetDetailsForMovieSet(*(item->GetVideoInfoTag()), item->GetArt());
  if (item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  return SetImportForItem(item, import);
}

bool CMovieSetImportHandler::UpdateImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  if (m_db.SetDetailsForMovieSet(*(item->GetVideoInfoTag()), item->GetArt(),
                                 item->GetVideoInfoTag()->m_iDbId) <= 0)
    return false;

  return true;
}

bool CMovieSetImportHandler::RemoveImportedItem(const CMediaImport& import, const CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return false;

  m_db.DeleteSet(item->GetVideoInfoTag()->m_iDbId);

  return true;
}

bool CMovieSetImportHandler::GetLocalItems(CVideoDatabase& videodb,
                                           const CMediaImport& import,
                                           std::vector<CFileItemPtr>& items) const
{
  CFileItemList movieSets;
  if (!videodb.GetSetsByWhere("videodb://movies/sets/?imported&import=" +
                                  CURL::Encode(import.GetPath()),
                              CDatabase::Filter(), movieSets, false))
    return false;

  items.insert(items.begin(), movieSets.cbegin(), movieSets.cend());

  return true;
}

std::set<Field> CMovieSetImportHandler::IgnoreDifferences() const
{
  return {FieldActor,
          FieldAirDate,
          FieldAlbum,
          FieldArtist,
          FieldCountry,
          FieldDirector,
          FieldEpisodeNumber,
          FieldEpisodeNumberSpecialSort,
          FieldFilename,
          FieldGenre,
          FieldInProgress,
          FieldLastPlayed,
          FieldMPAA,
          FieldOriginalTitle,
          FieldPath,
          FieldPlaycount,
          FieldPlotOutline,
          FieldProductionCode,
          FieldRating,
          FieldSeason,
          FieldSeasonSpecialSort,
          FieldSet,
          FieldSortTitle,
          FieldStudio,
          FieldTag,
          FieldTagline,
          FieldTime,
          FieldTop250,
          FieldTrackNumber,
          FieldTrailer,
          FieldTvShowStatus,
          FieldTvShowTitle,
          FieldUniqueId,
          FieldUserRating,
          FieldWriter};
}
