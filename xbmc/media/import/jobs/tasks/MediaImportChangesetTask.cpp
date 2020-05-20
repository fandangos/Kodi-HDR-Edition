/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportChangesetTask.h"

#include "guilib/LocalizeStrings.h"
#include "media/import/IMediaImportHandler.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportChangesetTask::CMediaImportChangesetTask(const CMediaImport& import,
                                                     MediaImportHandlerPtr importHandler,
                                                     const std::vector<CFileItemPtr>& localItems,
                                                     const ChangesetItems& retrievedItems,
                                                     bool partialChangeset /* = false */)
  : IMediaImportTask("CMediaImportChangesetTask", import),
    m_importHandler(importHandler),
    m_localItems(localItems),
    m_retrievedItems(retrievedItems),
    m_partialChangeset(partialChangeset)
{
}

bool CMediaImportChangesetTask::DoWork()
{
  size_t total = m_retrievedItems.size();
  size_t progress = 0;

  // prepare the progress bar
  PrepareProgressBarHandle(StringUtils::Format(g_localizeStrings.Get(39559).c_str(),
                                               m_import.GetSource().GetFriendlyName().c_str()));
  SetProgressText(StringUtils::Format(
      g_localizeStrings.Get(39560).c_str(),
      CMediaTypes::GetPluralLocalization(m_importHandler->GetMediaType()).c_str()));

  if (ShouldCancel(0, total))
    return false;

  const auto& settings = m_import.Settings();

  for (ChangesetItems::iterator item = m_retrievedItems.begin(); item != m_retrievedItems.end();)
  {
    // check if we should cancel
    if (ShouldCancel(progress, total))
      return false;

    if (item->second == nullptr)
      item->first = MediaImportChangesetType::None;
    else
    {
      // try to find a local item matching the retrieved item
      CFileItemPtr matchingLocalItem =
          m_importHandler->FindMatchingLocalItem(m_import, item->second.get(), m_localItems);

      // no matching local item found
      if (matchingLocalItem == nullptr)
      {
        if (!m_partialChangeset)
          item->first = MediaImportChangesetType::Added;
        else
        {
          if (item->first == MediaImportChangesetType::None ||
              item->first == MediaImportChangesetType::Added)
            item->first = MediaImportChangesetType::Added;
          else
          {
            // cannot change or remove an imported item without a matching local item
            if (item->first == MediaImportChangesetType::Changed)
            {
              m_logger->warn(
                  "unable to change item {} from {} because there's no matching local item",
                  item->second->GetPath(), m_import);
            }

            item->first = MediaImportChangesetType::None;
          }
        }
      }
      else
      {
        if (m_partialChangeset)
        {
          // we can't add an item that has already been imported so we'll update it
          if (item->first == MediaImportChangesetType::None ||
              item->first == MediaImportChangesetType::Added)
            item->first = MediaImportChangesetType::Changed;
          // if the item should be removed we need to replace it with the matching local item
          else if (item->first == MediaImportChangesetType::Removed)
            item->second = matchingLocalItem;
        }

        // remove the matching item from the local list so that the imported item is not considered non-existant
        m_localItems.erase(std::remove(m_localItems.begin(), m_localItems.end(), matchingLocalItem),
                           m_localItems.end());

        // ignoring items to be removed
        if (item->first != MediaImportChangesetType::Removed)
        {
          // nothing to do if we don't need to update imported items
          if (!settings->UpdateImportedMediaItems())
            item->first = MediaImportChangesetType::None;
          // otherwise determine the changeset type and prepare the imported item
          else
          {
            // determine the changeset state of the item
            item->first = m_importHandler->DetermineChangeset(m_import, item->second.get(),
                                                              matchingLocalItem);

            // if the imported item has changed prepare it for updating
            if (item->first != MediaImportChangesetType::None)
              m_importHandler->PrepareImportedItem(m_import, item->second.get(), matchingLocalItem);
          }
        }
      }
    }

    // if the changeset state couldn't be determined, ignore the item
    if (item->first == MediaImportChangesetType::None)
      item = m_retrievedItems.erase(item);
    else
      ++item;

    ++progress;
    SetProgress(progress, total);
  }

  if (!m_partialChangeset)
  {
    // all local items left need to be removed
    for (const auto& item : m_localItems)
      m_retrievedItems.push_back(std::make_pair(MediaImportChangesetType::Removed, item));
  }

  m_localItems.clear();

  return true;
}
