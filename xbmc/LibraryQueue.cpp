/*
 *  Copyright (C) 2014-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "LibraryQueue.h"

#include "GUIUserMessages.h"
#include "ServiceBroker.h"
#include "Util.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "threads/SingleLock.h"
#include "video/jobs/VideoLibraryCleaningJob.h"
#include "video/jobs/VideoLibraryJob.h"
#include "video/jobs/VideoLibraryMarkWatchedJob.h"
#include "video/jobs/VideoLibraryRefreshingJob.h"
#include "video/jobs/VideoLibraryResetResumePointJob.h"
#include "video/jobs/VideoLibraryScanningJob.h"

#include <utility>

CLibraryQueue::CLibraryQueue()
  : CJobQueue(false, 1, CJob::PRIORITY_LOW),
    m_jobs()
{ }

CLibraryQueue::~CLibraryQueue()
{
  CSingleLock lock(m_critical);
  m_jobs.clear();
}

CLibraryQueue& CLibraryQueue::GetInstance()
{
  static CLibraryQueue s_instance;
  return s_instance;
}

void CLibraryQueue::ScanVideoLibrary(const std::string& directory, bool scanAll /* = false */ , bool showProgress /* = true */)
{
  AddJob(new CVideoLibraryScanningJob(directory, scanAll, showProgress));
}

bool CLibraryQueue::IsScanningLibrary() const
{
  // check if the library is being cleaned synchronously
  if (m_cleaning)
    return true;

  // check if the library is being scanned asynchronously
  LibraryJobMap::const_iterator scanningJobs = m_jobs.find("VideoLibraryScanningJob");
  if (scanningJobs != m_jobs.end() && !scanningJobs->second.empty())
    return true;

  // check if the library is being cleaned asynchronously
  LibraryJobMap::const_iterator cleaningJobs = m_jobs.find("VideoLibraryCleaningJob");
  if (cleaningJobs != m_jobs.end() && !cleaningJobs->second.empty())
    return true;

  return false;
}

void CLibraryQueue::StopLibraryScanning()
{
  CSingleLock lock(m_critical);
  LibraryJobMap::const_iterator scanningJobs = m_jobs.find("VideoLibraryScanningJob");
  if (scanningJobs == m_jobs.end())
    return;

  // get a copy of the scanning jobs because CancelJob() will modify m_scanningJobs
  LibraryJobs tmpScanningJobs(scanningJobs->second.begin(), scanningJobs->second.end());

  // cancel all scanning jobs
  for (LibraryJobs::const_iterator job = tmpScanningJobs.begin(); job != tmpScanningJobs.end(); ++job)
    CancelJob(*job);
  Refresh();
}

void CLibraryQueue::CleanVideoLibrary(const std::set<int>& paths /* = std::set<int>() */, bool asynchronous /* = true */, CGUIDialogProgressBarHandle* progressBar /* = NULL */)
{
  CVideoLibraryCleaningJob* cleaningJob = new CVideoLibraryCleaningJob(paths, progressBar);

  if (asynchronous)
    AddJob(cleaningJob);
  else
  {
    m_modal = true;
    m_cleaning = true;
    cleaningJob->DoWork();

    delete cleaningJob;
    m_cleaning = false;
    m_modal = false;
    Refresh();
  }
}

void CLibraryQueue::CleanVideoLibraryModal(const std::set<int>& paths /* = std::set<int>() */)
{
  // we can't perform a modal library cleaning if other jobs are running
  if (IsRunning())
    return;

  m_modal = true;
  m_cleaning = true;
  CVideoLibraryCleaningJob cleaningJob(paths, true);
  cleaningJob.DoWork();
  m_cleaning = false;
  m_modal = false;
  Refresh();
}

void CLibraryQueue::RefreshItem(CFileItemPtr item, bool ignoreNfo /* = false */, bool forceRefresh /* = true */, bool refreshAll /* = false */, const std::string& searchTitle /* = "" */)
{
  AddJob(new CVideoLibraryRefreshingJob(item, forceRefresh, refreshAll, ignoreNfo, searchTitle));
}

bool CLibraryQueue::RefreshItemModal(CFileItemPtr item, bool forceRefresh /* = true */, bool refreshAll /* = false */)
{
  // we can't perform a modal library cleaning if other jobs are running
  if (IsRunning())
    return false;

  m_modal = true;
  CVideoLibraryRefreshingJob refreshingJob(item, forceRefresh, refreshAll);

  bool result = refreshingJob.DoModal();
  m_modal = false;

  return result;
}

void CLibraryQueue::MarkAsWatched(const CFileItemPtr &item, bool watched)
{
  if (item == NULL)
    return;

  AddJob(new CVideoLibraryMarkWatchedJob(item, watched));
}

void CLibraryQueue::ResetResumePoint(const CFileItemPtr item)
{
  if (item == nullptr)
    return;

  AddJob(new CVideoLibraryResetResumePointJob(item));
}

void CLibraryQueue::AddJob(CLibraryJob *job)
{
  if (job == NULL)
    return;

  CSingleLock lock(m_critical);
  if (!CJobQueue::AddJob(job))
    return;

  // add the job to our list of queued/running jobs
  std::string jobType = job->GetType();
  LibraryJobMap::iterator jobsIt = m_jobs.find(jobType);
  if (jobsIt == m_jobs.end())
  {
    LibraryJobs jobs;
    jobs.insert(job);
    m_jobs.insert(std::make_pair(jobType, jobs));
  }
  else
    jobsIt->second.insert(job);
}

void CLibraryQueue::CancelJob(CLibraryJob* job)
{
  if (job == NULL)
    return;

  CSingleLock lock(m_critical);
  // remember the job type needed later because the job might be deleted
  // in the call to CJobQueue::CancelJob()
  std::string jobType;
  if (job->GetType() != NULL)
    jobType = job->GetType();

  // check if the job supports cancellation and cancel it
  if (job->CanBeCancelled())
    job->Cancel();

  // remove the job from the job queue
  CJobQueue::CancelJob(job);

  // remove the job from our list of queued/running jobs
  LibraryJobMap::iterator jobsIt = m_jobs.find(jobType);
  if (jobsIt != m_jobs.end())
    jobsIt->second.erase(job);
}

void CLibraryQueue::CancelAllJobs()
{
  CSingleLock lock(m_critical);
  CJobQueue::CancelJobs();

  // remove all scanning jobs
  m_jobs.clear();
}

bool CLibraryQueue::IsRunning() const
{
  return CJobQueue::IsProcessing() || m_modal;
}

  void CLibraryQueue::Refresh()
{
  CUtil::DeleteVideoDatabaseDirectoryCache();
  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
}

void CLibraryQueue::OnJobComplete(unsigned int jobID, bool success, CJob* job)
{
  if (success)
  {
    if (QueueEmpty())
      Refresh();
  }

  {
    CSingleLock lock(m_critical);
    // remove the job from our list of queued/running jobs
    LibraryJobMap::iterator jobsIt = m_jobs.find(job->GetType());
    if (jobsIt != m_jobs.end())
      jobsIt->second.erase(static_cast<CLibraryJob*>(job));
  }

  return CJobQueue::OnJobComplete(jobID, success, job);
}
