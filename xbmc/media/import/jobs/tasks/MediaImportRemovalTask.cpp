/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportRemovalTask.h"

#include "guilib/LocalizeStrings.h"
#include "media/MediaType.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/MediaImportManager.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportRemovalTask::CMediaImportRemovalTask(const CMediaImport& import,
                                                 MediaImportHandlerPtr importHandler)
  : IMediaImportTask("CMediaImportRemovalTask", import), m_importHandler(importHandler)
{
}

bool CMediaImportRemovalTask::DoWork()
{
  if (m_importHandler == nullptr)
    return false;

  // prepare the progress bar
  PrepareProgressBarHandle(StringUtils::Format(
      g_localizeStrings.Get(39566).c_str(), CMediaTypes::ToLabel(m_import.GetMediaTypes()).c_str(),
      m_import.GetSource().GetFriendlyName().c_str()));
  SetProgressText("");

  m_logger->info("removing imported {} items from {}", m_importHandler->GetMediaType(),
                 m_import.GetSource());

  return m_importHandler->RemoveImportedItems(m_import);
}
