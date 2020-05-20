/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "LibraryQueue.h"
#include "media/MediaType.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/IMediaImportHandlerManager.h"
#include "media/import/IMediaImporter.h"
#include "media/import/IMediaImporterManager.h"
#include "media/import/MediaImport.h"
#include "media/import/MediaImportChangesetTypes.h"
#include "media/import/jobs/MediaImportTaskTypes.h"
#include "utils/StaticLoggerBase.h"

#include <string>
#include <vector>

class CFileItem;
class CGUIDialogProgressBarHandle;
class CMediaImportSource;
class IMediaImportTask;
class IMediaImportTaskCallback;

class CMediaImportTaskProcessorJob : public CLibraryJob, protected CStaticLoggerBase
{
public:
  virtual ~CMediaImportTaskProcessorJob();

  static CMediaImportTaskProcessorJob* Import(
      const CMediaImport& import,
      bool automatically,
      const IMediaImporterManager* importerManager,
      const IMediaImportHandlerManager* importHandlerManager,
      IMediaImportTaskCallback* callback);

  static CMediaImportTaskProcessorJob* ChangeImportedItems(
      const CMediaImport& import,
      const ChangesetItems& items,
      const IMediaImportHandlerManager* importHandlerManager,
      IMediaImportTaskCallback* callback);

  static CMediaImportTaskProcessorJob* UpdateImportedItemOnSource(
      const CMediaImport& import,
      const CFileItem& item,
      const IMediaImporterManager* importerManager,
      IMediaImportTaskCallback* callback);

  static CMediaImportTaskProcessorJob* Cleanup(
      const CMediaImportSource& source,
      const std::vector<CMediaImport>& imports,
      const IMediaImportHandlerManager* importHandlerManager,
      IMediaImportTaskCallback* callback);
  static CMediaImportTaskProcessorJob* Cleanup(
      const CMediaImport& import,
      const IMediaImportHandlerManager* importHandlerManager,
      IMediaImportTaskCallback* callback);

  static CMediaImportTaskProcessorJob* Remove(
      const CMediaImportSource& source,
      const std::vector<CMediaImport>& imports,
      const IMediaImportHandlerManager* importHandlerManager,
      IMediaImportTaskCallback* callback);
  static CMediaImportTaskProcessorJob* Remove(
      const CMediaImport& import,
      const IMediaImportHandlerManager* importHandlerManager,
      IMediaImportTaskCallback* callback);

  const std::string& GetPath() const { return m_path; }

  void SetTask(IMediaImportTask* task);
  void ResetTask();
  const IMediaImportTask* GetCurrentTask() const { return m_task; }

  /*!
   * \brief Get the progress bar handle instance used by the import task
   */
  CGUIDialogProgressBarHandle* GetProgressBarHandle(const std::string& title = "");

  // implementation of CJob
  virtual bool DoWork();
  virtual const char* GetType() const { return "MediaImportTaskProcessorJob"; }
  virtual bool operator==(const CJob* job) const;

protected:
  CMediaImportTaskProcessorJob(const std::string& path,
                               const IMediaImporterManager* importerManager,
                               const IMediaImportHandlerManager* importHandlerManager,
                               IMediaImportTaskCallback* callback,
                               bool hasProgress);

  bool ProcessTask();
  bool ProcessTask(IMediaImportTask* task);
  void ProcessLocalItemsRetrievalTasks();
  void ProcessImportItemsRetrievalTasks();
  void ProcessChangesetTasks();
  void ProcessSynchronisationTasks();
  void ProcessCleanupTasks();
  void ProcessRemovalTasks();
  bool OnTaskComplete(bool success, const IMediaImportTask* task);

  bool AddImport(const CMediaImport& import, std::vector<MediaImportTaskType> tasksToBeProcessed);

  const IMediaImporterManager* m_importerManager;
  const IMediaImportHandlerManager* m_importHandlerManager;
  IMediaImportTaskCallback* m_callback;
  IMediaImportTask* m_task;
  bool m_hasProgress;
  CGUIDialogProgressBarHandle* m_progress;
  std::string m_path;

  typedef struct MediaImportTaskData
  {
    CMediaImport m_import;

    bool m_partialChangeset;

    typedef struct MediaTypeTaskData
    {
      MediaType m_mediaType;
      MediaImportHandlerConstPtr m_importHandler;
      std::vector<CFileItemPtr> m_localItems;
      ChangesetItems m_importedItems;
    } MediaTypeTaskData;

    std::vector<MediaTypeTaskData> m_mediaTypeData;
  } MediaImportTaskData;

  using MediaImportTaskKey = std::pair<std::string, GroupedMediaTypes>;
  std::map<MediaImportTaskKey, MediaImportTaskData> m_importTaskData;
  std::vector<MediaImportTaskType> m_taskTypesToBeProcessed;
};
