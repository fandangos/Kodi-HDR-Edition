/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportTaskProcessorJob.h"

#include "ServiceBroker.h"
#include "dialogs/GUIDialogExtendedProgressBar.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/MediaImportManager.h"
#include "media/import/jobs/tasks/IMediaImportTask.h"
#include "media/import/jobs/tasks/MediaImportChangesetTask.h"
#include "media/import/jobs/tasks/MediaImportCleanupTask.h"
#include "media/import/jobs/tasks/MediaImportImportItemsRetrievalTask.h"
#include "media/import/jobs/tasks/MediaImportLocalItemsRetrievalTask.h"
#include "media/import/jobs/tasks/MediaImportRemovalTask.h"
#include "media/import/jobs/tasks/MediaImportSynchronisationTask.h"
#include "media/import/jobs/tasks/MediaImportUpdateTask.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/PerformanceMeasurement.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

#include <fmt/ostream.h>

CMediaImportTaskProcessorJob::CMediaImportTaskProcessorJob(
    const std::string& path,
    const IMediaImporterManager* importerManager,
    const IMediaImportHandlerManager* importHandlerManager,
    IMediaImportTaskCallback* callback,
    bool hasProgress)
  : CStaticLoggerBase("CMediaImportTaskProcessorJob"),
    m_importerManager(importerManager),
    m_importHandlerManager(importHandlerManager),
    m_callback(callback),
    m_task(nullptr),
    m_hasProgress(hasProgress),
    m_progress(nullptr),
    m_path(path),
    m_importTaskData(),
    m_taskTypesToBeProcessed()
{
  if (m_hasProgress && CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
                           CSettings::SETTING_VIDEOLIBRARY_BACKGROUNDUPDATE))
    m_hasProgress = false;
}

CMediaImportTaskProcessorJob::~CMediaImportTaskProcessorJob()
{
  if (m_progress != nullptr)
    m_progress->MarkFinished();
}

CMediaImportTaskProcessorJob* CMediaImportTaskProcessorJob::Import(
    const CMediaImport& import,
    bool automatically,
    const IMediaImporterManager* importerManager,
    const IMediaImportHandlerManager* importHandlerManager,
    IMediaImportTaskCallback* callback)
{
  if (importerManager == nullptr)
  {
    s_logger->error("invalid media importer manager implementation");
    return nullptr;
  }

  if (importHandlerManager == nullptr)
  {
    s_logger->error("invalid media import handler manager implementation");
    return nullptr;
  }

  CMediaImport tmpImport = import;
  if (automatically && tmpImport.Settings()->Load() &&
      tmpImport.Settings()->GetImportTrigger() != MediaImportTrigger::Auto)
  {
    s_logger->debug("automatic import of items from {} is disabled", import);
    return nullptr;
  }

  auto processorJob = new CMediaImportTaskProcessorJob(
      import.GetSource().GetIdentifier(), importerManager, importHandlerManager, callback, true);
  if (!processorJob->AddImport(import, {}))
  {
    s_logger->warn("failed to import items from {}", import);
    return nullptr;
  }

  return processorJob;
}

CMediaImportTaskProcessorJob* CMediaImportTaskProcessorJob::ChangeImportedItems(
    const CMediaImport& import,
    const ChangesetItems& items,
    const IMediaImportHandlerManager* importHandlerManager,
    IMediaImportTaskCallback* callback)
{
  if (importHandlerManager == nullptr)
  {
    s_logger->error("invalid media import handler manager implementation");
    return nullptr;
  }

  // add the import to the map of imports to process
  // and remember to perform a partial changeset
  MediaImportTaskData importTaskData = {import, true};

  // prepare the media type data
  std::map<MediaType, MediaImportTaskData::MediaTypeTaskData> mediaTypeDataMap;
  for (const auto& mediaType : import.GetMediaTypes())
  {
    // get the import handler
    const auto importer = importHandlerManager->GetImportHandler(mediaType);
    if (importer == nullptr)
      continue;

    mediaTypeDataMap.insert(
        std::make_pair(mediaType, MediaImportTaskData::MediaTypeTaskData{mediaType, importer}));
  }

  for (const auto& changedItem : items)
  {
    if (changedItem.second == nullptr)
      continue;

    // check the media type
    auto mediaTypeDataIt = mediaTypeDataMap.find(changedItem.second->GetMediaType());
    if (mediaTypeDataIt == mediaTypeDataMap.end())
      continue;

    mediaTypeDataIt->second.m_importedItems.push_back(changedItem);
  }

  for (const auto& mediaTypeData : mediaTypeDataMap)
  {
    // ignore media type data without any changed items
    if (mediaTypeData.second.m_importedItems.empty())
      continue;

    importTaskData.m_mediaTypeData.push_back(mediaTypeData.second);
  }

  auto processorJob = new CMediaImportTaskProcessorJob(import.GetSource().GetIdentifier(), nullptr,
                                                       importHandlerManager, callback, false);
  processorJob->m_importTaskData.emplace(std::make_pair(import.GetPath(), import.GetMediaTypes()),
                                         importTaskData);

  // get all local items
  processorJob->m_taskTypesToBeProcessed.push_back(MediaImportTaskType::LocalItemsRetrieval);

  // determine the partial changeset for the given items
  processorJob->m_taskTypesToBeProcessed.push_back(MediaImportTaskType::Changeset);

  // do a sychronisation and cleanup
  processorJob->m_taskTypesToBeProcessed.push_back(MediaImportTaskType::Synchronisation);
  processorJob->m_taskTypesToBeProcessed.push_back(MediaImportTaskType::Cleanup);

  return processorJob;
}

CMediaImportTaskProcessorJob* CMediaImportTaskProcessorJob::UpdateImportedItemOnSource(
    const CMediaImport& import,
    const CFileItem& item,
    const IMediaImporterManager* importerManager,
    IMediaImportTaskCallback* callback)
{
  if (importerManager == nullptr)
  {
    s_logger->error("invalid media importer manager implementation");
    return nullptr;
  }

  auto processorJob = new CMediaImportTaskProcessorJob(import.GetSource().GetIdentifier(),
                                                       importerManager, nullptr, callback, false);

  auto updateTask = new CMediaImportUpdateTask(import, item, importerManager);
  processorJob->m_taskTypesToBeProcessed.push_back(MediaImportTaskType::Update);
  processorJob->SetTask(updateTask);

  return processorJob;
}

CMediaImportTaskProcessorJob* CMediaImportTaskProcessorJob::Cleanup(
    const CMediaImportSource& source,
    const std::vector<CMediaImport>& imports,
    const IMediaImportHandlerManager* importHandlerManager,
    IMediaImportTaskCallback* callback)
{
  if (importHandlerManager == nullptr)
  {
    s_logger->error("invalid media import handler manager implementation");
    return nullptr;
  }

  if (imports.empty())
    return nullptr;

  auto processorJob = new CMediaImportTaskProcessorJob(source.GetIdentifier(), nullptr,
                                                       importHandlerManager, callback, true);

  bool added = false;
  std::vector<MediaImportTaskType> tasksToBeProcessed;
  tasksToBeProcessed.push_back(MediaImportTaskType::Cleanup);
  for (const auto& import : imports)
  {
    if (!processorJob->AddImport(import, tasksToBeProcessed))
    {
      s_logger->warn("failed to cleanup imported items from {}", import);
      continue;
    }

    added |= true;
  }

  if (!added)
  {
    delete processorJob;
    return nullptr;
  }

  return processorJob;
}

CMediaImportTaskProcessorJob* CMediaImportTaskProcessorJob::Cleanup(
    const CMediaImport& import,
    const IMediaImportHandlerManager* importHandlerManager,
    IMediaImportTaskCallback* callback)
{
  return Cleanup(import.GetSource(), {import}, importHandlerManager, callback);
}

CMediaImportTaskProcessorJob* CMediaImportTaskProcessorJob::Remove(
    const CMediaImportSource& source,
    const std::vector<CMediaImport>& imports,
    const IMediaImportHandlerManager* importHandlerManager,
    IMediaImportTaskCallback* callback)
{
  if (importHandlerManager == nullptr)
  {
    s_logger->error("invalid media import handler manager implementation");
    return nullptr;
  }

  auto processorJob = new CMediaImportTaskProcessorJob(source.GetIdentifier(), nullptr,
                                                       importHandlerManager, callback, true);

  std::vector<MediaImportTaskType> tasksToBeProcessed;
  tasksToBeProcessed.push_back(MediaImportTaskType::Removal);
  for (const auto& import : imports)
  {
    if (!processorJob->AddImport(import, tasksToBeProcessed))
    {
      s_logger->warn("failed to remove imported items from {}", import);
      continue;
    }
  }

  return processorJob;
}

CMediaImportTaskProcessorJob* CMediaImportTaskProcessorJob::Remove(
    const CMediaImport& import,
    const IMediaImportHandlerManager* importHandlerManager,
    IMediaImportTaskCallback* callback)
{
  return Remove(import.GetSource(), {import}, importHandlerManager, callback);
}

void CMediaImportTaskProcessorJob::SetTask(IMediaImportTask* task)
{
  m_task = task;
  if (m_task != nullptr)
    m_task->SetProcessorJob(this);
}

void CMediaImportTaskProcessorJob::ResetTask()
{
  if (m_task != nullptr)
    m_task->SetProcessorJob(nullptr);

  m_task = nullptr;
}

CGUIDialogProgressBarHandle* CMediaImportTaskProcessorJob::GetProgressBarHandle(
    const std::string& title /* = "" */)
{
  if (!m_hasProgress)
    return nullptr;

  if (m_progress == nullptr)
  {
    auto dialog = static_cast<CGUIDialogExtendedProgressBar*>(
        CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_EXT_PROGRESS));
    if (dialog != nullptr)
      m_progress = dialog->GetHandle(title);
  }
  else if (!title.empty())
    m_progress->SetTitle(title);

  return m_progress;
}

bool CMediaImportTaskProcessorJob::DoWork()
{
  return ProcessTask();
}

bool CMediaImportTaskProcessorJob::operator==(const CJob* job) const
{
  if (strcmp(job->GetType(), GetType()) != 0)
    return false;

  const CMediaImportTaskProcessorJob* rjob = dynamic_cast<const CMediaImportTaskProcessorJob*>(job);
  if (rjob == nullptr)
    return false;

  // compare the base properties
  bool equalBase = m_path == rjob->m_path && m_callback == rjob->m_callback &&
                   m_task == rjob->m_task && m_progress == rjob->m_progress &&
                   m_importTaskData.size() == rjob->m_importTaskData.size();
  if (!equalBase)
    return false;

  // compare the import task data
  for (const auto& importTaskData : m_importTaskData)
  {
    const auto& rjobImportTaskData = rjob->m_importTaskData.find(importTaskData.first);
    if (rjobImportTaskData == rjob->m_importTaskData.end())
      return false;

    if (importTaskData.second.m_import != rjobImportTaskData->second.m_import ||
        importTaskData.second.m_partialChangeset != rjobImportTaskData->second.m_partialChangeset)
      return false;
  }

  return true;
}

bool CMediaImportTaskProcessorJob::ProcessTask()
{
  // check if no task is set and there are no more task types to be performed
  if (m_task == nullptr && m_taskTypesToBeProcessed.empty())
    return true;

  // if a task is set perform it
  if (m_task != nullptr)
  {
    // get an independent reference to m_task as it will be reset by ProcessTask()
    IMediaImportTask* task = m_task;

    // let the current task do its work
    bool success = ProcessTask(task);

    // delete the previously processed task
    delete task;

    return success;
  }

  // if there are no media imports there's nothing to be processed
  if (m_importTaskData.empty())
    return true;

  // there's no task set and still task types to perform so go through all the media imports and perform the next task type
  MediaImportTaskType currentTaskType = m_taskTypesToBeProcessed.front();

  switch (currentTaskType)
  {
    case MediaImportTaskType::LocalItemsRetrieval:
      ProcessLocalItemsRetrievalTasks();
      break;

    case MediaImportTaskType::ImportItemsRetrieval:
      ProcessImportItemsRetrievalTasks();
      break;

    case MediaImportTaskType::Changeset:
      ProcessChangesetTasks();
      break;

    case MediaImportTaskType::Synchronisation:
      ProcessSynchronisationTasks();
      break;

    case MediaImportTaskType::Cleanup:
      ProcessCleanupTasks();
      break;

    case MediaImportTaskType::Removal:
      ProcessRemovalTasks();
      break;

    default:
      s_logger->warn("unknown import task type {}", static_cast<int>(currentTaskType));
      return false;
  }

  // remove the processed task type from the list of task types to process
  m_taskTypesToBeProcessed.erase(m_taskTypesToBeProcessed.begin());

  // let's do another round of processing in case there's more to do
  return ProcessTask();
}

bool CMediaImportTaskProcessorJob::ProcessTask(IMediaImportTask* task)
{
  if (task == nullptr)
    return false;

  SetTask(task);

  const auto& import = task->GetImport();

  s_logger->debug("processing {} task from {}...", MediaImportTaskTypes::ToString(task->GetType()),
                  import);

  // performance measurements
  CPerformanceMeasurement<> perf;

  // let the current task do its work
  bool success = task->DoWork();

  // the task has been completed
  success &= OnTaskComplete(success, task);

  perf.Stop();
  s_logger->debug("processing {} task from {} took {} s",
                  MediaImportTaskTypes::ToString(task->GetType()), import,
                  perf.GetDurationInSeconds());

  ResetTask();

  return success;
}

void CMediaImportTaskProcessorJob::ProcessLocalItemsRetrievalTasks()
{
  for (auto&& taskData = m_importTaskData.begin(); taskData != m_importTaskData.end();)
  {
    std::map<MediaType, MediaImportHandlerPtr> mediaImportHandlers;
    for (const auto& mediaTypeData : taskData->second.m_mediaTypeData)
      mediaImportHandlers.insert(
          std::make_pair(mediaTypeData.m_mediaType,
                         MediaImportHandlerPtr(mediaTypeData.m_importHandler->Create())));

    const CMediaImport& import = taskData->second.m_import;
    CMediaImportLocalItemsRetrievalTask* localItemsRetrievalTask =
        new CMediaImportLocalItemsRetrievalTask(import, mediaImportHandlers);

    // if processing the task failed remove the import (no cleanup needed)
    s_logger->info("starting local items retrieval task for items from {}...", import);
    if (!ProcessTask(localItemsRetrievalTask))
    {
      s_logger->error("local items retrieval task for items from {} failed", import);
      m_importTaskData.erase(taskData++);
      delete localItemsRetrievalTask;
      continue;
    }

    // get the local items
    for (auto& mediaTypeData : taskData->second.m_mediaTypeData)
      mediaTypeData.m_localItems =
          localItemsRetrievalTask->GetLocalItems(mediaTypeData.m_mediaType);

    delete localItemsRetrievalTask;
    ++taskData;
  }
}

void CMediaImportTaskProcessorJob::ProcessImportItemsRetrievalTasks()
{
  for (auto&& taskData = m_importTaskData.begin(); taskData != m_importTaskData.end();)
  {
    const CMediaImport& import = taskData->second.m_import;
    CMediaImportImportItemsRetrievalTask* importItemsRetrievalTask =
        new CMediaImportImportItemsRetrievalTask(import, m_importerManager);

    // add all previously imported items
    const auto& mediaTypes = import.GetMediaTypes();
    for (auto& mediaTypeData : taskData->second.m_mediaTypeData)
    {
      if (std::find(mediaTypes.begin(), mediaTypes.end(), mediaTypeData.m_mediaType) !=
          mediaTypes.end())
        importItemsRetrievalTask->SetLocalItems(mediaTypeData.m_localItems,
                                                mediaTypeData.m_mediaType);
    }

    // if processing the task failed remove the import (no cleanup needed)
    s_logger->info("starting import items retrieval task for items from {}...", import);
    if (!ProcessTask(importItemsRetrievalTask))
    {
      s_logger->warn("import items retrieval task for items from {} failed", import);
      m_importTaskData.erase(taskData++);
      delete importItemsRetrievalTask;
      continue;
    }

    // get back the import (in case it has changed)
    taskData->second.m_import = importItemsRetrievalTask->GetImport();

    // check whether to perform a full or partial changeset
    taskData->second.m_partialChangeset = importItemsRetrievalTask->IsChangeset();

    // get the retrieved items
    for (auto& mediaTypeData : taskData->second.m_mediaTypeData)
      mediaTypeData.m_importedItems =
          importItemsRetrievalTask->GetRetrievedItems(mediaTypeData.m_mediaType);

    delete importItemsRetrievalTask;
    ++taskData;
  }
}

void CMediaImportTaskProcessorJob::ProcessChangesetTasks()
{
  for (auto&& taskData : m_importTaskData)
  {
    const CMediaImport& import = taskData.second.m_import;
    for (auto&& mediaTypeData = taskData.second.m_mediaTypeData.begin();
         mediaTypeData != taskData.second.m_mediaTypeData.end();)
    {
      CMediaImportChangesetTask* changesetTask = new CMediaImportChangesetTask(
          import, MediaImportHandlerPtr(mediaTypeData->m_importHandler->Create()),
          mediaTypeData->m_localItems, mediaTypeData->m_importedItems,
          taskData.second.m_partialChangeset);

      // if processing the task failed remove the import (no cleanup needed)
      s_logger->info("starting import changeset task for {} items from {}...",
                     mediaTypeData->m_mediaType.c_str(), import);
      if (!ProcessTask(changesetTask))
      {
        s_logger->warn("import changeset task for {} items from {} failed",
                       mediaTypeData->m_mediaType.c_str(), import);
        mediaTypeData = taskData.second.m_mediaTypeData.erase(mediaTypeData);
        delete changesetTask;
        continue;
      }

      // get the changeset
      mediaTypeData->m_importedItems = changesetTask->GetChangeset();
      delete changesetTask;

      // if the changeset is empty there is nothing else to do
      if (mediaTypeData->m_importedItems.empty())
      {
        s_logger->debug("no {} items from {} changed", mediaTypeData->m_mediaType.c_str(), import);
      }

      ++mediaTypeData;
    }
  }
}

void CMediaImportTaskProcessorJob::ProcessSynchronisationTasks()
{
  // go through all imports and their media types in the proper order and perform the synchronisation
  for (auto& taskData : m_importTaskData)
  {
    // go through all media types in the proper order and perform the synchronisation
    for (auto&& mediaTypeData = taskData.second.m_mediaTypeData.begin();
         mediaTypeData != taskData.second.m_mediaTypeData.end(); ++mediaTypeData)
    {
      const CMediaImport& import = taskData.second.m_import;
      CMediaImportSynchronisationTask* synchronisationTask = new CMediaImportSynchronisationTask(
          import, MediaImportHandlerPtr(mediaTypeData->m_importHandler->Create()),
          mediaTypeData->m_importedItems);

      // if processing the task failed remove the import (no cleanup needed)
      s_logger->info("starting import synchronisation task for {} items from {}...",
                     mediaTypeData->m_mediaType.c_str(), import);
      if (!ProcessTask(synchronisationTask))
      {
        s_logger->warn("import changeset task for {} items from {} failed",
                       mediaTypeData->m_mediaType.c_str(), import);
        // don't remove the import even though it failed because we should run the cleanup
      }

      delete synchronisationTask;
    }
  }
}

void CMediaImportTaskProcessorJob::ProcessCleanupTasks()
{
  // go through all imports and their media types in the proper order and clean them up
  for (auto& taskData : m_importTaskData)
  {
    // go through all media types in the proper order and clean them up
    for (auto&& mediaTypeData = taskData.second.m_mediaTypeData.rbegin();
         mediaTypeData != taskData.second.m_mediaTypeData.rend(); ++mediaTypeData)
    {
      const CMediaImport& import = taskData.second.m_import;
      CMediaImportCleanupTask* cleanupTask = new CMediaImportCleanupTask(
          import, MediaImportHandlerPtr(mediaTypeData->m_importHandler->Create()));

      // if processing the task failed remove the import (no cleanup needed)
      s_logger->info("starting import cleanup task for {} items from {}...",
                     mediaTypeData->m_mediaType.c_str(), import);
      if (!ProcessTask(cleanupTask))
      {
        s_logger->warn("import cleanup task for {} items from {} failed",
                       mediaTypeData->m_mediaType.c_str(), import);
      }

      delete cleanupTask;
    }
  }
}

void CMediaImportTaskProcessorJob::ProcessRemovalTasks()
{
  // go through all imports and their media types in the proper order and remove them
  for (auto& taskData : m_importTaskData)
  {
    // go through all media types in the proper order and remove them
    for (auto&& mediaTypeData = taskData.second.m_mediaTypeData.rbegin();
         mediaTypeData != taskData.second.m_mediaTypeData.rend(); ++mediaTypeData)
    {
      const CMediaImport& import = taskData.second.m_import;
      CMediaImportRemovalTask* removalTask = new CMediaImportRemovalTask(
          import, MediaImportHandlerPtr(mediaTypeData->m_importHandler->Create()));

      // if processing the task failed remove the import
      s_logger->info("starting import removal task for {} items from {}...",
                     mediaTypeData->m_mediaType.c_str(), import);
      if (!ProcessTask(removalTask))
        s_logger->warn("import removal task for {} items from {} failed",
                       mediaTypeData->m_mediaType.c_str(), import);

      delete removalTask;
    }
  }
}

bool CMediaImportTaskProcessorJob::OnTaskComplete(bool success, const IMediaImportTask* task)
{
  if (m_callback == nullptr)
    return true;

  return m_callback->OnTaskComplete(success, task);
}

bool CMediaImportTaskProcessorJob::AddImport(const CMediaImport& import,
                                             std::vector<MediaImportTaskType> tasksToBeProcessed)
{
  if (m_importHandlerManager == nullptr)
  {
    s_logger->error("invalid media import handler manager implementation");
    return false;
  }

  // check if an import with that path and media type already exists
  const auto& itImportTaskData =
      m_importTaskData.find(std::make_pair(import.GetPath(), import.GetMediaTypes()));
  if (itImportTaskData != m_importTaskData.end())
    return false;

  // add the import to the map of imports to process
  MediaImportTaskData importTaskData = {import};

  // get the import handlers
  for (const auto& mediaType : import.GetMediaTypes())
  {
    MediaImportTaskData::MediaTypeTaskData mediaTypeData = {
        mediaType, m_importHandlerManager->GetImportHandler(mediaType)};
    if (mediaTypeData.m_importHandler == nullptr)
      return false;

    importTaskData.m_mediaTypeData.push_back(mediaTypeData);
  }

  m_importTaskData.emplace(std::make_pair(import.GetPath(), import.GetMediaTypes()),
                           importTaskData);

  // determine the tasks (and their order) to process
  if (tasksToBeProcessed.empty())
  {
    // always do a retrieval
    tasksToBeProcessed.push_back(MediaImportTaskType::LocalItemsRetrieval);
    tasksToBeProcessed.push_back(MediaImportTaskType::ImportItemsRetrieval);

    // also add the changeset task (even though it might not be performed depending on the importer being used)
    tasksToBeProcessed.push_back(MediaImportTaskType::Changeset);

    // always do a sychronisation and cleanup
    tasksToBeProcessed.push_back(MediaImportTaskType::Synchronisation);
    tasksToBeProcessed.push_back(MediaImportTaskType::Cleanup);
  }

  // now synchronise the list of tasks to be processed for this import with the one for all imports
  if (m_taskTypesToBeProcessed.empty())
    m_taskTypesToBeProcessed = tasksToBeProcessed;
  else
  {
    size_t start_index = 0;
    for (const auto& newTask : tasksToBeProcessed)
    {
      bool found = false;
      for (std::vector<MediaImportTaskType>::const_iterator task =
               m_taskTypesToBeProcessed.begin() + start_index;
           task != m_taskTypesToBeProcessed.end(); ++task)
      {
        if (*task == newTask)
        {
          start_index = std::distance<std::vector<MediaImportTaskType>::const_iterator>(
              m_taskTypesToBeProcessed.begin(), task);
          found = true;
          break;
        }
      }

      // if the new task hasn't been found insert it at the earliest position
      if (!found)
      {
        m_taskTypesToBeProcessed.insert(m_taskTypesToBeProcessed.begin() + start_index, newTask);
        start_index += 1;
      }
    }
  }

  return true;
}
