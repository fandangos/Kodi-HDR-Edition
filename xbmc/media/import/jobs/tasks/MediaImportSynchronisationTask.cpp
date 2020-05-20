/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportSynchronisationTask.h"

#include "guilib/LocalizeStrings.h"
#include "media/MediaType.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/MediaImportManager.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportSynchronisationTask::CMediaImportSynchronisationTask(
    const CMediaImport& import, MediaImportHandlerPtr importHandler, const ChangesetItems& items)
  : IMediaImportTask("CMediaImportSynchronisationTask", import),
    m_importHandler(importHandler),
    m_items(items)
{
}

bool CMediaImportSynchronisationTask::DoWork()
{
  if (m_importHandler == nullptr)
    return false;

  // nothing to do if there are no items to synchronise
  if (m_items.empty())
    return true;

  if (!m_importHandler->StartSynchronisation(m_import))
  {
    m_logger->info("failed to initialize synchronisation of imported {} items from {}",
                   m_importHandler->GetMediaType(), m_import.GetSource());
    return false;
  }

  // prepare the progress bar
  PrepareProgressBarHandle(StringUtils::Format(
      g_localizeStrings.Get(39561).c_str(), CMediaTypes::ToLabel(m_import.GetMediaTypes()).c_str(),
      m_import.GetSource().GetFriendlyName().c_str()));
  SetProgressText("");

  if (ShouldCancel(0, m_items.size()))
    return false;

  m_logger->info("handling {} imported {} items from {}", m_items.size(),
                 m_importHandler->GetMediaType(), m_import.GetSource());
  // handle the imported items of a specific media type
  size_t total = m_items.size();
  size_t progress = 0;
  for (const auto& item : m_items)
  {
    // check if we should cancel
    if (ShouldCancel(progress, total))
      return false;

    // get the item label to be used in the progress bar text
    std::string itemLabel = m_importHandler->GetItemLabel(item.second.get());

    // process the item depending on its changeset state
    switch (item.first)
    {
      case MediaImportChangesetType::Added:
        SetProgressText(
            StringUtils::Format(g_localizeStrings.Get(39562).c_str(), itemLabel.c_str()));
        m_importHandler->AddImportedItem(m_import, item.second.get());
        break;

      case MediaImportChangesetType::Changed:
        SetProgressText(
            StringUtils::Format(g_localizeStrings.Get(39563).c_str(), itemLabel.c_str()));
        m_importHandler->UpdateImportedItem(m_import, item.second.get());
        break;

      case MediaImportChangesetType::Removed:
        SetProgressText(
            StringUtils::Format(g_localizeStrings.Get(39564).c_str(), itemLabel.c_str()));
        m_importHandler->RemoveImportedItem(m_import, item.second.get());
        break;

      case MediaImportChangesetType::None:
        break;

      default:
        m_logger->warn("ignoring imported item with unknown changeset type {}", item.first);
        break;
    }

    ++progress;
    SetProgress(progress, total);
  }

  if (!m_importHandler->FinishSynchronisation(m_import))
  {
    m_logger->info("failed to finalize synchronisation of imported {} items from {}",
                   m_importHandler->GetMediaType(), m_import.GetSource());
    return false;
  }

  return true;
}
