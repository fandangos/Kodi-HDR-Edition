/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "FileItem.h"
#include "media/import/jobs/tasks/IMediaImportTask.h"

#include <memory>

class IMediaImporter;

class CMediaImportUpdateTask : public IMediaImportTask
{
public:
  CMediaImportUpdateTask(const CMediaImport& import,
                         const CFileItem& item,
                         const IMediaImporterManager* importerManager);
  virtual ~CMediaImportUpdateTask() = default;

  /*!
   * \brief Get the IMediaImporter instance used by the import job
   */
  const std::shared_ptr<IMediaImporter> GetImporter() const { return m_importer; }

  const CFileItem& GetItem() const { return m_item; }

  // implementation of IMediaImportTask
  MediaImportTaskType GetType() const override { return MediaImportTaskType::Update; }
  bool DoWork() override;

protected:
  const IMediaImporterManager* m_importerManager;
  std::shared_ptr<IMediaImporter> m_importer;
  CFileItem m_item;
};
