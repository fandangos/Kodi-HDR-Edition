/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportCleanupTask.h"

#include "guilib/LocalizeStrings.h"
#include "media/MediaType.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/MediaImportManager.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportCleanupTask::CMediaImportCleanupTask(const CMediaImport& import,
                                                 MediaImportHandlerPtr importHandler)
  : IMediaImportTask("CMediaImportCleanupTask", import), m_importHandler(importHandler)
{
}

bool CMediaImportCleanupTask::DoWork()
{
  if (m_importHandler == nullptr)
    return false;

  // prepare the progress bar
  PrepareProgressBarHandle(StringUtils::Format(
      g_localizeStrings.Get(39569).c_str(), CMediaTypes::ToLabel(m_import.GetMediaTypes()).c_str(),
      m_import.GetSource().GetFriendlyName().c_str()));
  SetProgressText("");

  m_logger->info("cleaning up imported {} items from {}", m_importHandler->GetMediaType(),
                 m_import.GetSource());

  if (!m_importHandler->CleanupImportedItems(m_import))
    return false;

  // now make sure the items are enabled
  m_importHandler->SetImportedItemsEnabled(m_import, true);

  return true;
}
