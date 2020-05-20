/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportUpdateTask.h"

#include "ServiceBroker.h"
#include "media/import/IMediaImporter.h"
#include "media/import/MediaImportManager.h"
#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportUpdateTask::CMediaImportUpdateTask(const CMediaImport& import,
                                               const CFileItem& item,
                                               const IMediaImporterManager* importerManager)
  : IMediaImportTask("CMediaImportUpdateTask", import),
    m_importerManager(importerManager),
    m_importer(),
    m_item(item)
{
}

bool CMediaImportUpdateTask::DoWork()
{
  if (m_importer == nullptr)
  {
    if (m_importerManager == nullptr)
    {
      m_logger->error("invalid media importer manager implementation");
      return false;
    }

    // look for an importer than can handle the given path
    m_importer = m_importerManager->GetImporterBySource(m_import.GetSource());
    if (m_importer == nullptr)
    {
      m_logger->error("no importer capable of handling source {} found", m_import.GetSource());
      return false;
    }
  }

  return m_importer->UpdateOnSource(this);
}
