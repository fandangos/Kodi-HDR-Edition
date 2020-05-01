/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/MediaType.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/MediaImportChangesetTypes.h"
#include "media/import/jobs/tasks/IMediaImportTask.h"

#include <map>

class CMediaImportLocalItemsRetrievalTask : public IMediaImportTask
{
public:
  CMediaImportLocalItemsRetrievalTask(const CMediaImport& import,
                                      std::map<MediaType, MediaImportHandlerPtr> importHandlers);
  virtual ~CMediaImportLocalItemsRetrievalTask() = default;

  /*!
   * \brief Get the media type of the media import
   */
  const GroupedMediaTypes& GetMediaTypes() const { return GetImport().GetMediaTypes(); }

  /*!
  * \brief Get a list of previously imported items
  *
  * \param mediaType media type of the previously imported items
  * \return list of previously imported items
  */
  const std::vector<CFileItemPtr>& GetLocalItems(const MediaType& mediaType) const
  {
    return m_localItems.find(mediaType)->second;
  }

  // implementation of IMediaImportTask
  MediaImportTaskType GetType() const override { return MediaImportTaskType::LocalItemsRetrieval; }
  bool DoWork() override;

protected:
  std::map<MediaType, MediaImportHandlerPtr> m_importHandlers;
  std::map<MediaType, std::vector<CFileItemPtr>> m_localItems;
};
