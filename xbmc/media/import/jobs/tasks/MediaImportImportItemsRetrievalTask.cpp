/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportImportItemsRetrievalTask.h"

#include "guilib/LocalizeStrings.h"
#include "media/import/IMediaImporter.h"
#include "media/import/MediaImportManager.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportImportItemsRetrievalTask::CMediaImportImportItemsRetrievalTask(
    const CMediaImport& import, const IMediaImporterManager* importerManager)
  : IMediaImportTask("CMediaImportImportItemsRetrievalTask", import),
    m_importerManager(importerManager),
    m_importer(),
    m_retrievedItems(),
    m_isChangeset(false)
{
  // pre-fill the item maps with all media types to be retrieved
  for (const auto& mediaType : import.GetMediaTypes())
  {
    m_localItems.insert(std::make_pair(mediaType, std::vector<CFileItemPtr>()));
    m_retrievedItems.insert(std::make_pair(mediaType, ChangesetItems()));
  }
}

bool CMediaImportImportItemsRetrievalTask::DoWork()
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

  PrepareProgressBarHandle(StringUtils::Format(g_localizeStrings.Get(39558).c_str(),
                                               m_import.GetSource().GetFriendlyName()));

  return m_importer->Import(this);
}

std::vector<CFileItemPtr> CMediaImportImportItemsRetrievalTask::GetLocalItems(
    const MediaType& mediaType)
{
  auto localItems = m_localItems.find(mediaType);
  if (localItems == m_localItems.end())
    return {};

  return localItems->second;
}

void CMediaImportImportItemsRetrievalTask::SetLocalItems(const std::vector<CFileItemPtr>& items,
                                                         const MediaType& mediaType)
{
  auto localItems = m_localItems.find(mediaType);
  if (localItems == m_localItems.end())
    return;

  localItems->second = items;
}

void CMediaImportImportItemsRetrievalTask::AddItem(
    const CFileItemPtr& item,
    const MediaType& mediaType,
    MediaImportChangesetType changesetType /* = MediaImportChangesetTypeNone */)
{
  auto retrievedItems = m_retrievedItems.find(mediaType);
  if (retrievedItems == m_retrievedItems.end())
    return;

  retrievedItems->second.push_back(std::make_pair(changesetType, item));
}

void CMediaImportImportItemsRetrievalTask::AddItems(
    const std::vector<CFileItemPtr>& items,
    const MediaType& mediaType,
    MediaImportChangesetType changesetType /* = MediaImportChangesetTypeNone */)
{
  for (const auto& item : items)
    AddItem(item, mediaType, changesetType);
}

void CMediaImportImportItemsRetrievalTask::SetItems(const ChangesetItems& items,
                                                    const MediaType& mediaType)
{
  auto retrievedItems = m_retrievedItems.find(mediaType);
  if (retrievedItems == m_retrievedItems.end())
    return;

  retrievedItems->second = items;
}
