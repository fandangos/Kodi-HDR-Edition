/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "dialogs/GUIDialogExtendedProgressBar.h"
#include "media/import/MediaImport.h"
#include "media/import/jobs/MediaImportTaskProcessorJob.h"
#include "media/import/jobs/MediaImportTaskTypes.h"
#include "utils/logtypes.h"

/*!
 * \brief Interface of a media importer capable of importing media items from
 * a specific source into the local library.
 */
class IMediaImportTask
{
public:
  virtual ~IMediaImportTask() = default;

  /*!
   * \brief Gets the import processed by the import task.
   */
  CMediaImport& GetImport() { return m_import; }

  /*!
   * \brief Gets the import processed by the import task.
   */
  const CMediaImport& GetImport() const { return m_import; }

  /*!
   * \brief Gets the processor job which is executing the import task.
   */
  CMediaImportTaskProcessorJob* GetProcessorJob() const { return m_processorJob; }

  /*!
   * \brief Sets the processor job which will be executing the import task.
   */
  void SetProcessorJob(CMediaImportTaskProcessorJob* processorJob)
  {
    m_processorJob = processorJob;
  }

  /*!
   * \brief Get the progress bar handle instance used by the import task.
   */
  CGUIDialogProgressBarHandle* GetProgressBarHandle() const { return m_progress; }

  /*!
  * \brief Gets the type of the import task.
  */
  virtual MediaImportTaskType GetType() const = 0;

  /*!
   * \brief Executes the import task.
   *
   * \return True if the task succeeded, false otherwise
   */
  virtual bool DoWork() = 0;

  /*
   * \brief Updates the progress of the task and checks if it should be cancelled.
   *
   * \param progress Current progress
   * \param total Total progress steps
   * \return True if the task should be cancelled, false otherwise
   */
  bool ShouldCancel(unsigned int progress, unsigned int total) const;

  /*
   * \brief Sets the title of the progress indicator.
   */
  void SetProgressTitle(const std::string& title);

  /*
  * \brief Sets the text/description of the progress indicator.
  */
  void SetProgressText(const std::string& text);

  /*
   * \brief Sets the progress of the progress indicator.
   *
   * \param progress Current progress
   * \param total Total progress steps
   */
  void SetProgress(int progress, int total);

protected:
  IMediaImportTask(const std::string& name, const CMediaImport& import);

  /*
   * \brief Prepares a progress indicator with the given title set.
   *
   * \param title Title to be set in the progress indicator.
   */
  void PrepareProgressBarHandle(const std::string& title);

  Logger m_logger;

  CMediaImport m_import;
  CMediaImportTaskProcessorJob* m_processorJob;
  CGUIDialogProgressBarHandle* m_progress;
};

class IMediaImportTaskCallback
{
public:
  virtual ~IMediaImportTaskCallback() = default;

  /*
   * \brief Callback for completed tasks.
   *
   * \param success Whether the task succeeded or failed.
   * \param task The task that has completed
   * \return True if the callback succeeded, false otherwise
   */
  virtual bool OnTaskComplete(bool success, const IMediaImportTask* task) = 0;

protected:
  IMediaImportTaskCallback() = default;
};
