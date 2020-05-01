/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MusicVideoImportHandler.h"

#include "FileItem.h"
#include "media/import/MediaImport.h"
#include "video/VideoDatabase.h"

bool CMusicVideoImportHandler::AddImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr)
    return false;

  PrepareItem(import, item);

  item->GetVideoInfoTag()->m_iDbId =
      m_db.SetDetailsForMusicVideo(item->GetPath(), *(item->GetVideoInfoTag()), item->GetArt());
  if (item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  SetDetailsForFile(item, false);
  return SetImportForItem(item, import);
}

bool CMusicVideoImportHandler::UpdateImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  if (m_db.SetDetailsForMusicVideo(item->GetPath(), *(item->GetVideoInfoTag()), item->GetArt(),
                                   item->GetVideoInfoTag()->m_iDbId) <= 0)
    return false;

  if (import.Settings()->UpdatePlaybackMetadataFromSource())
    SetDetailsForFile(item, true);

  return true;
}

bool CMusicVideoImportHandler::RemoveImportedItem(const CMediaImport& import, const CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return false;

  m_db.DeleteMusicVideo(item->GetVideoInfoTag()->m_iDbId);
  RemoveFile(m_db, item);

  return true;
}

bool CMusicVideoImportHandler::GetLocalItems(CVideoDatabase& videodb,
                                             const CMediaImport& import,
                                             std::vector<CFileItemPtr>& items) const
{
  CFileItemList musicvideos;
  if (!videodb.GetMusicVideosByWhere(
          "videodb://musicvideos/titles/?imported&import=" + CURL::Encode(import.GetPath()),
          CDatabase::Filter(), musicvideos, true, SortDescription(),
          import.Settings()->UpdateImportedMediaItems() ? VideoDbDetailsAll : VideoDbDetailsNone))
    return false;

  items.insert(items.begin(), musicvideos.cbegin(), musicvideos.cend());

  return true;
}

std::set<Field> CMusicVideoImportHandler::IgnoreDifferences() const
{
  return {FieldActor,         FieldCountry,
          FieldEpisodeNumber, FieldEpisodeNumberSpecialSort,
          FieldMPAA,          FieldOriginalTitle,
          FieldPlotOutline,   FieldProductionCode,
          FieldSeason,        FieldSeasonSpecialSort,
          FieldSet,           FieldSortTitle,
          FieldTagline,       FieldTop250,
          FieldTrackNumber,   FieldTrailer,
          FieldTvShowStatus,  FieldTvShowTitle,
          FieldWriter};
}
