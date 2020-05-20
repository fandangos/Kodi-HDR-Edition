/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "FileItem.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/MediaImportChangesetTypes.h"
#include "media/import/jobs/tasks/IMediaImportTask.h"

#include <vector>

class CMediaImportChangesetTask : public IMediaImportTask
{
public:
  CMediaImportChangesetTask(const CMediaImport& import,
                            MediaImportHandlerPtr importHandler,
                            const std::vector<CFileItemPtr>& localItems,
                            const ChangesetItems& retrievedItems,
                            bool partialChangeset = false);
  virtual ~CMediaImportChangesetTask() = default;

  const ChangesetItems& GetChangeset() const { return m_retrievedItems; }

  // implementation of IMediaImportTask
  MediaImportTaskType GetType() const override { return MediaImportTaskType::Changeset; }
  bool DoWork() override;

protected:
  MediaImportHandlerPtr m_importHandler;
  std::vector<CFileItemPtr> m_localItems;
  ChangesetItems m_retrievedItems;
  bool m_partialChangeset;
};
