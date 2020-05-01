/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportLocalItemsRetrievalTask.h"

#include "guilib/LocalizeStrings.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/IMediaImporter.h"
#include "media/import/MediaImportManager.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportLocalItemsRetrievalTask::CMediaImportLocalItemsRetrievalTask(
    const CMediaImport& import, std::map<MediaType, MediaImportHandlerPtr> importHandlers)
  : IMediaImportTask("CMediaImportLocalItemsRetrievalTask", import),
    m_importHandlers(importHandlers),
    m_localItems()
{
  // pre-fill the item maps with all media types to be retrieved
  for (const auto& mediaType : import.GetMediaTypes())
    m_localItems.insert(std::make_pair(mediaType, std::vector<CFileItemPtr>()));
}

bool CMediaImportLocalItemsRetrievalTask::DoWork()
{
  PrepareProgressBarHandle(StringUtils::Format(g_localizeStrings.Get(39558).c_str(),
                                               m_import.GetSource().GetFriendlyName().c_str()));

  // first get a list of items previously imported from the media import
  for (const auto& importHandler : m_importHandlers)
  {
    if (importHandler.second == nullptr)
      continue;

    std::vector<CFileItemPtr> localItems;
    if (!importHandler.second->GetLocalItems(m_import, localItems))
    {
      m_logger->error("failed to get previously imported items of type {} from {}",
                      importHandler.first, m_import);
      return false;
    }

    m_localItems[importHandler.first] = localItems;
  }

  return true;
}
