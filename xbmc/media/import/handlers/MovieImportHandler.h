/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/handlers/VideoImportHandler.h"

class CMovieImportHandler : public CVideoImportHandler
{
public:
  CMovieImportHandler(const IMediaImportHandlerManager* importHandlerManager)
    : CVideoImportHandler(importHandlerManager)
  {
  }
  virtual ~CMovieImportHandler() = default;

  CMovieImportHandler* Create() const override
  {
    return new CMovieImportHandler(m_importHandlerManager);
  }

  MediaType GetMediaType() const override { return MediaTypeMovie; }
  GroupedMediaTypes GetGroupedMediaTypes() const override
  {
    return {MediaTypeMovie, MediaTypeVideoCollection};
  }

  bool AddImportedItem(const CMediaImport& import, CFileItem* item) override;
  bool UpdateImportedItem(const CMediaImport& import, CFileItem* item) override;
  bool RemoveImportedItem(const CMediaImport& import, const CFileItem* item) override;
  bool CleanupImportedItems(const CMediaImport& import) override { return true; }

protected:
  bool GetLocalItems(CVideoDatabase& videodb,
                     const CMediaImport& import,
                     std::vector<CFileItemPtr>& items) const override;

  std::set<Field> IgnoreDifferences() const override;
};
