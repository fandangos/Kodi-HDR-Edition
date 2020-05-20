/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/MediaType.h"
#include "media/import/IMediaImporter.h"
#include "media/import/IMediaImporterManager.h"
#include "media/import/MediaImportChangesetTypes.h"
#include "media/import/jobs/tasks/IMediaImportTask.h"

#include <map>
#include <memory>

class CMediaImportImportItemsRetrievalTask : public IMediaImportTask
{
public:
  CMediaImportImportItemsRetrievalTask(const CMediaImport& import,
                                       const IMediaImporterManager* importerManager);
  virtual ~CMediaImportImportItemsRetrievalTask() = default;

  /*!
   * \brief Get the IMediaImporter instance used by the import job
   */
  const std::shared_ptr<IMediaImporter> GetImporter() const { return m_importer; }

  /*!
   * \brief Get the media type of the media import
   */
  const GroupedMediaTypes& GetMediaTypes() const { return GetImport().GetMediaTypes(); }

  /*!
   * \brief Get a list of previously imported items
   *
   * \param mediaType media type of the imported items
   * \return list of previously imported items
   */
  std::vector<CFileItemPtr> GetLocalItems(const MediaType& mediaType);

  /*!
  * \brief Add a list of previously imported items of a specific media type
  *
  * \param items previously imported items
  * \param mediaType media type of the items
  */
  void SetLocalItems(const std::vector<CFileItemPtr>& items, const MediaType& mediaType);

  /*!
   * \brief Get a list of imported items
   *
   * \param mediaType media type of the imported items
   * \return list of imported items
   */
  const ChangesetItems& GetRetrievedItems(const MediaType& mediaType) const
  {
    return m_retrievedItems.find(mediaType)->second;
  }

  /*!
   * \brief Whether the retrieved items are already a changeset or not
   */
  bool IsChangeset() const { return m_isChangeset; }

  /*!
   * \brief Add an imported item of a specific changeset type
   *
   * \param item imported item
   * \param mediaType media type of the item
   * \param changesetType changeset type of the imported item
   */
  void AddItem(const CFileItemPtr& item,
               const MediaType& mediaType,
               MediaImportChangesetType changesetType = MediaImportChangesetType::None);

  /*!
  * \brief Add a list of imported items of a specific changeset type
  *
  * \param items imported items
   * \param mediaType media type of the items
  * \param changesetType changeset type of the imported items
  */
  void AddItems(const std::vector<CFileItemPtr>& items,
                const MediaType& mediaType,
                MediaImportChangesetType changesetType = MediaImportChangesetType::None);

  /*!
   * \brief Add a list of imported items of a specific media type
   *
   * \param items imported items
   * \param mediaType media type of the items
   */
  void SetItems(const ChangesetItems& items, const MediaType& mediaType);

  /*!
   * \brief Specify whether the retrieved items are a changeset or not
   */
  void SetChangeset(bool isChangeset) { m_isChangeset = isChangeset; }

  // implementation of IMediaImportTask
  MediaImportTaskType GetType() const override { return MediaImportTaskType::ImportItemsRetrieval; }
  bool DoWork() override;

protected:
  const IMediaImporterManager* m_importerManager;
  std::shared_ptr<IMediaImporter> m_importer;
  std::map<MediaType, std::vector<CFileItemPtr>> m_localItems;
  std::map<MediaType, ChangesetItems> m_retrievedItems;
  bool m_isChangeset;
};
