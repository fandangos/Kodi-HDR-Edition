/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "dbwrappers/Database.h"
#include "media/import/IMediaImportHandler.h"
#include "utils/StaticLoggerBase.h"
#include "video/VideoDatabase.h"
#include "video/VideoThumbLoader.h"

class CFileItem;

class CVideoImportHandler : public IMediaImportHandler, protected CStaticLoggerBase
{
public:
  virtual ~CVideoImportHandler() = default;

  std::string GetItemLabel(const CFileItem* item) const override;

  bool GetLocalItems(const CMediaImport& import, std::vector<CFileItemPtr>& items) const override;

  bool StartChangeset(const CMediaImport& import) override;
  bool FinishChangeset(const CMediaImport& import) override;
  CFileItemPtr FindMatchingLocalItem(const CMediaImport& import,
                                     const CFileItem* item,
                                     const std::vector<CFileItemPtr>& localItems) const override;
  MediaImportChangesetType DetermineChangeset(const CMediaImport& import,
                                              const CFileItem* item,
                                              const CFileItemPtr& localItem) override;
  void PrepareImportedItem(const CMediaImport& import,
                           CFileItem* item,
                           const CFileItemPtr& localItem) const override;

  bool StartSynchronisation(const CMediaImport& import) override;
  bool FinishSynchronisation(const CMediaImport& import) override;

  bool RemoveImportedItems(const CMediaImport& import) override;

  void SetImportedItemsEnabled(const CMediaImport& import, bool enable) override;

protected:
  CVideoImportHandler(const IMediaImportHandlerManager* importHandlerManager);

  virtual bool GetLocalItems(CVideoDatabase& videodb,
                             const CMediaImport& import,
                             std::vector<CFileItemPtr>& items) const = 0;

  virtual std::set<Field> IgnoreDifferences() const { return std::set<Field>(); }

  virtual bool RemoveImportedItems(CVideoDatabase& videodb, const CMediaImport& import) const;

  void PrepareItem(const CMediaImport& import, CFileItem* pItem);
  void SetDetailsForFile(const CFileItem* pItem, bool reset);
  bool SetImportForItem(const CFileItem* pItem, const CMediaImport& import);
  void RemoveFile(CVideoDatabase& videodb, const CFileItem* item) const;

  bool Compare(const CFileItem* originalItem,
               const CFileItem* newItem,
               bool allMetadata = true,
               bool playbackMetadata = true) const;

  static void RemoveAutoArtwork(CGUIListItem::ArtMap& artwork,
                                const std::set<std::string>& parentPrefixes);

  mutable CVideoDatabase m_db;
  CVideoThumbLoader m_thumbLoader;
  std::set<std::string> m_sourcePaths;
  std::map<std::string, int> m_importPathIds;
};
