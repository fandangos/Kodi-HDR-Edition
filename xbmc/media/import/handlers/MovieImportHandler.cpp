/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MovieImportHandler.h"

#include "FileItem.h"
#include "media/import/MediaImport.h"
#include "video/VideoDatabase.h"

bool CMovieImportHandler::AddImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr)
    return false;

  PrepareItem(import, item);

  item->GetVideoInfoTag()->m_iDbId =
      m_db.SetDetailsForMovie(item->GetPath(), *(item->GetVideoInfoTag()), item->GetArt());
  if (item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  SetDetailsForFile(item, false);
  return SetImportForItem(item, import);
}

bool CMovieImportHandler::UpdateImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  if (m_db.SetDetailsForMovie(item->GetPath(), *(item->GetVideoInfoTag()), item->GetArt(),
                              item->GetVideoInfoTag()->m_iDbId) <= 0)
    return false;

  if (import.Settings()->UpdatePlaybackMetadataFromSource())
    SetDetailsForFile(item, true);

  return true;
}

bool CMovieImportHandler::RemoveImportedItem(const CMediaImport& import, const CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return false;

  m_db.DeleteMovie(item->GetVideoInfoTag()->m_iDbId);
  RemoveFile(m_db, item);

  return true;
}

bool CMovieImportHandler::GetLocalItems(CVideoDatabase& videodb,
                                        const CMediaImport& import,
                                        std::vector<CFileItemPtr>& items) const
{
  CFileItemList movies;
  if (!videodb.GetMoviesByWhere(
          "videodb://movies/titles/?imported&import=" + CURL::Encode(import.GetPath()),
          CDatabase::Filter(), movies, SortDescription(),
          import.Settings()->UpdateImportedMediaItems() ? VideoDbDetailsAll : VideoDbDetailsNone))
    return false;

  items.insert(items.begin(), movies.cbegin(), movies.cend());

  return true;
}

std::set<Field> CMovieImportHandler::IgnoreDifferences() const
{
  return {
      FieldAlbum,          FieldArtist,     FieldEpisodeNumber,     FieldEpisodeNumberSpecialSort,
      FieldProductionCode, FieldSeason,     FieldSeasonSpecialSort, FieldTrackNumber,
      FieldTvShowStatus,   FieldTvShowTitle};
}
