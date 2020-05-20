/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/handlers/VideoImportHandler.h"

class CTvShowImportHandler : public CVideoImportHandler
{
public:
  CTvShowImportHandler(const IMediaImportHandlerManager* importHandlerManager)
    : CVideoImportHandler(importHandlerManager)
  {
  }
  virtual ~CTvShowImportHandler() = default;

  CTvShowImportHandler* Create() const override
  {
    return new CTvShowImportHandler(m_importHandlerManager);
  }

  MediaType GetMediaType() const override { return MediaTypeTvShow; }
  MediaTypes GetRequiredMediaTypes() const override { return {MediaTypeEpisode}; }
  GroupedMediaTypes GetGroupedMediaTypes() const override
  {
    return {MediaTypeTvShow, MediaTypeSeason, MediaTypeEpisode};
  }

  CFileItemPtr FindMatchingLocalItem(const CMediaImport& import,
                                     const CFileItem* item,
                                     const std::vector<CFileItemPtr>& localItems) const override;

  bool AddImportedItem(const CMediaImport& import, CFileItem* item) override;
  bool UpdateImportedItem(const CMediaImport& import, CFileItem* item) override;
  bool RemoveImportedItem(const CMediaImport& import, const CFileItem* item) override;
  bool CleanupImportedItems(const CMediaImport& import) override;

protected:
  bool GetLocalItems(CVideoDatabase& videodb,
                     const CMediaImport& import,
                     std::vector<CFileItemPtr>& items) const override;

  std::set<Field> IgnoreDifferences() const override;

  bool RemoveImportedItems(CVideoDatabase& videodb, const CMediaImport& import) const override;
  void RemoveImportedItem(CVideoDatabase& videodb,
                          const CMediaImport& import,
                          const CFileItem* item) const;
};
