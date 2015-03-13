/*
 *  Copyright (C) 2014-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "FileItem.h"
#include "threads/CriticalSection.h"
#include "utils/JobManager.h"

#include <map>
#include <set>

class CGUIDialogProgressBarHandle;

/*!
\brief Basic implementation/interface of a CJob which interacts with a library.
*/
class CLibraryJob : public CJob
{
public:
  virtual ~CLibraryJob() { }

  /*!
  \brief Whether the job can be cancelled or not.
  */
  virtual bool CanBeCancelled() const { return false; }

  /*!
  \brief Tries to cancel the running job.

  \return True if the job was cancelled, false otherwise
  */
  virtual bool Cancel() { return false; }

  // implementation of CJob
  virtual const char *GetType() const { return "LibraryJob"; }
  virtual bool operator==(const CJob* job) const { return false; }

protected:
  CLibraryJob() { }
};

/*!
 \brief Queue for library jobs.

 The queue can only process a single job at any time and every job will be
 executed at the lowest priority.
 */
class CLibraryQueue : protected CJobQueue
{
public:
  ~CLibraryQueue() override;

  /*!
   \brief Gets the singleton instance of the library queue.
  */
  static CLibraryQueue& GetInstance();

  /*!
   \brief Enqueue a video library scan job.

   \param[in] directory Directory to scan
   \param[in] scanAll Ignore exclude setting for items. Defaults to false
   \param[in] showProgress Whether or not to show a progress dialog. Defaults to true
   */
  void ScanVideoLibrary(const std::string& directory, bool scanAll = false, bool showProgress = true);

  /*!
   \brief Check if a library scan is in progress.

   \return True if a scan is in progress, false otherwise
   */
  bool IsScanningLibrary() const;

  /*!
   \brief Stop and dequeue all scanning jobs.
   */
  void StopLibraryScanning();

  /*!
   \brief Enqueue a video library cleaning job.

   \param[in] paths Set with database IDs of paths to be cleaned
   \param[in] asynchronous Run the clean job asynchronously. Defaults to true
   \param[in] progressBar Progress bar to update in GUI. Defaults to NULL (no progress bar to update)
   */
  void CleanVideoLibrary(const std::set<int>& paths = std::set<int>(), bool asynchronous = true, CGUIDialogProgressBarHandle* progressBar = NULL);

  /*!
  \brief Executes a video library cleaning with a modal dialog.

  \param[in] paths Set with database IDs of paths to be cleaned
  */
  void CleanVideoLibraryModal(const std::set<int>& paths = std::set<int>());

  /*!
   \brief Enqueues a job to refresh the details of the given item.

   \param[inout] item Video item to be refreshed
   \param[in] ignoreNfo Whether or not to ignore local NFO files
   \param[in] forceRefresh Whether to force a complete refresh (including NFO or internet lookup)
   \param[in] refreshAll Whether to refresh all sub-items (in case of a tvshow)
   \param[in] searchTitle Title to use for the search (instead of determining it from the item's filename/path)
   */
  void RefreshItem(CFileItemPtr item, bool ignoreNfo = false, bool forceRefresh = true, bool refreshAll = false, const std::string& searchTitle = "");

  /*!
   \brief Refreshes the details of the given item with a modal dialog.

   \param[inout] item Video item to be refreshed
   \param[in] forceRefresh Whether to force a complete refresh (including NFO or internet lookup)
   \param[in] refreshAll Whether to refresh all sub-items (in case of a tvshow)
   \return True if the item has been successfully refreshed, false otherwise.
  */
  bool RefreshItemModal(CFileItemPtr item, bool forceRefresh = true, bool refreshAll = false);

  /*!
   \brief Queue a watched status update job.

   \param[in] item Item to update watched status for
   \param[in] watched New watched status
   */
  void MarkAsWatched(const CFileItemPtr &item, bool watched);

  /*!
   \brief Queue a reset resume point job.

   \param[in] item Item to reset the resume point for
   */
  void ResetResumePoint(const CFileItemPtr item);

  /*!
   \brief Adds the given job to the queue.

   \param[in] job Library job to be queued.
   */
  void AddJob(CLibraryJob* job);

  /*!
   \brief Cancels the given job and removes it from the queue.

   \param[in] job Library job to be canceled and removed from the queue.
   */
  void CancelJob(CLibraryJob* job);

  /*!
   \brief Cancels all running and queued jobs.
   */
  void CancelAllJobs();

  /*!
   \brief Whether any jobs are running or not.
   */
  bool IsRunning() const;

protected:
  // implementation of IJobCallback
  void OnJobComplete(unsigned int jobID, bool success, CJob *job) override;

  /*!
   \brief Notifies all to refresh the current listings.
   */
  void Refresh();

private:
  CLibraryQueue();
  CLibraryQueue(const CLibraryQueue&) = delete;
  CLibraryQueue const& operator=(CLibraryQueue const&) = delete;

  typedef std::set<CLibraryJob*> LibraryJobs;
  typedef std::map<std::string, LibraryJobs> LibraryJobMap;
  LibraryJobMap m_jobs;
  CCriticalSection m_critical;

  bool m_modal = false;
  bool m_cleaning = false;
};
