/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportSourceRegistrationJob.h"

#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportSourceRegistrationJob::CMediaImportSourceRegistrationJob(
    const CMediaImportSource& source, bool activate, const IMediaImporterManager* importerManager)
  : CMediaImportSourceReadyJob(source, importerManager, "CMediaImportSourceRegistrationJob"),
    m_activate(activate)
{
}

bool CMediaImportSourceRegistrationJob::DoWork()
{
  const auto& importer = GetImporter();
  if (importer == nullptr)
    return false;

  if (!importer->CanImport(m_source.GetIdentifier()))
  {
    m_logger->warn("importer \"{}\" cannot handle source {}", m_source.GetImporterId(), m_source);
    return false;
  }

  return CMediaImportSourceReadyJob::DoWork();
}
