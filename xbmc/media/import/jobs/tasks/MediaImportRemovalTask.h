/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/IMediaImportHandler.h"
#include "media/import/jobs/tasks/IMediaImportTask.h"

class CMediaImportRemovalTask : public IMediaImportTask
{
public:
  CMediaImportRemovalTask(const CMediaImport& import, MediaImportHandlerPtr importHandler);
  virtual ~CMediaImportRemovalTask() = default;

  // implementation of IMediaImportTask
  MediaImportTaskType GetType() const override { return MediaImportTaskType::Removal; }
  bool DoWork() override;

protected:
  MediaImportHandlerPtr m_importHandler;
};
