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

class CMediaImportSourceJobBase : public CJob
{
public:
  virtual ~CMediaImportSourceJobBase() = default;

  // implementation of CJob
  bool operator==(const CJob* other) const override
  {
    if (strcmp(other->GetType(), GetType()) != 0)
      return false;

    const CMediaImportSourceJobBase* otherSourceJob = dynamic_cast<const CMediaImportSourceJobBase*>(other);
    if (otherSourceJob == nullptr)
      return false;

    return m_source == otherSourceJob->m_source;
  }

  const CMediaImportSource& GetSource() const { return m_source; }

protected:
  CMediaImportSourceJobBase(const CMediaImportSource& source, const IMediaImporterManager* importerManager)
    : m_source(source)
    , m_importerManager(importerManager)
  { }

  CMediaImportSource m_source;
  const IMediaImporterManager* m_importerManager;
};
