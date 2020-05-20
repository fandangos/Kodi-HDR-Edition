/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/jobs/MediaImportSourceReadyJob.h"

class CMediaImportSourceActivationJob : public CMediaImportSourceReadyJob
{
public:
  CMediaImportSourceActivationJob(const CMediaImportSource& source,
                                  const IMediaImporterManager* importerManager)
    : CMediaImportSourceReadyJob(source, importerManager, "CMediaImportSourceActivationJob")
  {
  }
  virtual ~CMediaImportSourceActivationJob() = default;

  // implementation of CJob
  const char* GetType() const override { return "MediaImportSourceActivationJob"; }
};
