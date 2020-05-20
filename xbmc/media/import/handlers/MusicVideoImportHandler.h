/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/handlers/VideoImportHandler.h"

class CMusicVideoImportHandler : public CVideoImportHandler
{
public:
  CMusicVideoImportHandler(const IMediaImportHandlerManager* importHandlerManager)
    : CVideoImportHandler(importHandlerManager)
  {
  }
  virtual ~CMusicVideoImportHandler() = default;

  CMusicVideoImportHandler* Create() const override
  {
    return new CMusicVideoImportHandler(m_importHandlerManager);
  }

  MediaType GetMediaType() const override { return MediaTypeMusicVideo; }

  bool AddImportedItem(const CMediaImport& import, CFileItem* item) override;
  bool UpdateImportedItem(const CMediaImport& import, CFileItem* item) override;
  bool RemoveImportedItem(const CMediaImport& import, const CFileItem* item) override;
  bool CleanupImportedItems(const CMediaImport& import) override { return true; }

protected:
  virtual bool GetLocalItems(CVideoDatabase& videodb,
                             const CMediaImport& import,
                             std::vector<CFileItemPtr>& items) const override;

  virtual std::set<Field> IgnoreDifferences() const override;
};
