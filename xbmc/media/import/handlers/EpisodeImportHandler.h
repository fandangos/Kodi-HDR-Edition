/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/handlers/VideoImportHandler.h"

#include <map>
#include <set>
#include <string>

class CEpisodeImportHandler : public CVideoImportHandler
{
public:
  CEpisodeImportHandler(const IMediaImportHandlerManager* importHandlerManager)
    : CVideoImportHandler(importHandlerManager)
  {
  }
  virtual ~CEpisodeImportHandler() = default;

  CEpisodeImportHandler* Create() const override
  {
    return new CEpisodeImportHandler(m_importHandlerManager);
  }

  MediaType GetMediaType() const override { return MediaTypeEpisode; }
  GroupedMediaTypes GetGroupedMediaTypes() const override
  {
    return {MediaTypeTvShow, MediaTypeSeason, MediaTypeEpisode};
  }

  std::string GetItemLabel(const CFileItem* item) const override;

  bool StartSynchronisation(const CMediaImport& import) override;

  bool AddImportedItem(const CMediaImport& import, CFileItem* item) override;
  bool UpdateImportedItem(const CMediaImport& import, CFileItem* item) override;
  bool RemoveImportedItem(const CMediaImport& import, const CFileItem* item) override;
  bool CleanupImportedItems(const CMediaImport& import) override { return true; }

protected:
  bool GetLocalItems(CVideoDatabase& videodb,
                     const CMediaImport& import,
                     std::vector<CFileItemPtr>& items) const override;

  std::set<Field> IgnoreDifferences() const override;

private:
  int FindTvShowId(const CFileItem* episodeItem);

  typedef std::set<CFileItemPtr> TvShowsSet;
  typedef std::map<std::string, TvShowsSet> TvShowsMap;

  TvShowsMap m_tvshows;
};
