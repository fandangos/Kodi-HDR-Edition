/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/IMediaImporter.h"
#include "media/import/jobs/MediaImportSourceJobBase.h"
#include "utils/logtypes.h"

#include <memory>

class CMediaImportSourceReadyJob : public CMediaImportSourceJobBase
{
public:
  CMediaImportSourceReadyJob(const CMediaImportSource& source,
                             const IMediaImporterManager* importerManager,
                             const std::string& name = "CMediaImportSourceReadyJob");
  virtual ~CMediaImportSourceReadyJob() = default;

  // implementation of CJob
  const char* GetType() const override { return "MediaImportSourceReadyJob"; }

  // specialization of CMediaImportSourceJobBase
  bool DoWork() override;

  bool IsSourceReady() const { return m_ready; }

protected:
  std::shared_ptr<IMediaImporter> GetImporter();

private:
  std::shared_ptr<IMediaImporter> m_importer;
  bool m_ready;
};
