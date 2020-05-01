/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "IMediaImportTask.h"

#include "ServiceBroker.h"
#include "utils/log.h"

IMediaImportTask::IMediaImportTask(const std::string& name, const CMediaImport& import)
  : m_logger(CServiceBroker::GetLogging().GetLogger(name)),
    m_import(import),
    m_processorJob(nullptr),
    m_progress(nullptr)
{
}

bool IMediaImportTask::ShouldCancel(unsigned int progress, unsigned int total) const
{
  if (m_processorJob == nullptr)
    return false;

  return m_processorJob->ShouldCancel(progress, total);
}

void IMediaImportTask::SetProgressTitle(const std::string& title)
{
  if (m_progress != nullptr)
    m_progress->SetTitle(title);
}

void IMediaImportTask::SetProgressText(const std::string& text)
{
  if (m_progress != nullptr)
    m_progress->SetText(text);
}

void IMediaImportTask::SetProgress(int progress, int total)
{
  if (m_progress != nullptr)
    m_progress->SetProgress(progress, total);
}

void IMediaImportTask::PrepareProgressBarHandle(const std::string& title)
{
  if (m_processorJob != nullptr)
    m_progress = m_processorJob->GetProgressBarHandle(title);
}
