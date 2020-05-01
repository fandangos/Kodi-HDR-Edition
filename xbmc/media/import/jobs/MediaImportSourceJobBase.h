/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/IMediaImporterManager.h"
#include "media/import/MediaImportSource.h"
#include "utils/Job.h"
#include "utils/logtypes.h"

class CMediaImportSourceJobBase : public CJob
{
public:
  virtual ~CMediaImportSourceJobBase() = default;

  // implementation of CJob
  bool operator==(const CJob* other) const override;

  const CMediaImportSource& GetSource() const { return m_source; }

protected:
  CMediaImportSourceJobBase(const std::string& name,
                            const CMediaImportSource& source,
                            const IMediaImporterManager* importerManager);

  Logger m_logger;

  CMediaImportSource m_source;
  const IMediaImporterManager* m_importerManager;
};
