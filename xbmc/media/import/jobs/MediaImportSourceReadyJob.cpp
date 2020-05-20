/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportSourceReadyJob.h"

#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportSourceReadyJob::CMediaImportSourceReadyJob(
    const CMediaImportSource& source,
    const IMediaImporterManager* importerManager,
    const std::string& name /* = "CMediaImportSourceReadyJob" */)
  : CMediaImportSourceJobBase(name, source, importerManager), m_ready(false)
{
}

bool CMediaImportSourceReadyJob::DoWork()
{
  const auto& importer = GetImporter();
  if (importer == nullptr)
    return false;

  // check if the source is ready
  m_ready = importer->IsSourceReady(m_source);
  m_source.SetReady(m_ready);

  return true;
}

std::shared_ptr<IMediaImporter> CMediaImportSourceReadyJob::GetImporter()
{
  if (m_importer != nullptr)
    return m_importer;

  if (m_importerManager == nullptr)
  {
    m_logger->error("invalid media importer manager implementation for source {}", m_source);
    return nullptr;
  }

  m_importer = m_importerManager->GetImporterBySource(m_source);
  if (m_importer == nullptr)
  {
    m_logger->error("missing media importer for source {}", m_source);
    return nullptr;
  }

  return m_importer;
}
