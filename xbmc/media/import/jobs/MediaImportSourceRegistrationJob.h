/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/jobs/MediaImportSourceReadyJob.h"

class CMediaImportSourceRegistrationJob : public CMediaImportSourceReadyJob
{
public:
  CMediaImportSourceRegistrationJob(const CMediaImportSource& source,
                                    bool activate,
                                    const IMediaImporterManager* importerManager);
  virtual ~CMediaImportSourceRegistrationJob() = default;

  // implementation of CJob
  const char* GetType() const override { return "MediaImportSourceRegistrationJob"; }

  // specialization of CMediaImportSourceReadyJob
  bool DoWork() override;

  bool ActivateSource() const { return m_activate; }

private:
  const bool m_activate;
};
