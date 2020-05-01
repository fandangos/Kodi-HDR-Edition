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

class CMediaImportSynchronisationTask : public IMediaImportTask
{
public:
  CMediaImportSynchronisationTask(const CMediaImport& import,
                                  MediaImportHandlerPtr importHandler,
                                  const ChangesetItems& items);
  virtual ~CMediaImportSynchronisationTask() = default;

  /*!
  * \brief Get the media type being synchronised
  */
  MediaType GetMediaType() const { return m_importHandler->GetMediaType(); }

  // implementation of IMediaImportTask
  MediaImportTaskType GetType() const override { return MediaImportTaskType::Synchronisation; }
  bool DoWork() override;

protected:
  MediaImportHandlerPtr m_importHandler;
  ChangesetItems m_items;
};
