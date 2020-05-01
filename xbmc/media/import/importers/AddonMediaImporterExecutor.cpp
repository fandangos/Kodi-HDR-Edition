/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AddonMediaImporterExecutor.h"

#include "ServiceBroker.h"
#include "URL.h"
#include "addons/AddonManager.h"
#include "addons/IAddon.h"
#include "addons/MediaImporter.h"
#include "media/import/jobs/tasks/IMediaImportTask.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

CAddonMediaImporterExecutor::CAddonMediaImporterExecutor(
    const std::string& addonId,
    Action action,
    CAddonMediaImporter* importer /* = nullptr */,
    const std::string& actionName /* = "" */)
  : m_addonId(addonId),
    m_action(action),
    m_actionName(!actionName.empty() ? actionName : ActionToString(action)),
    m_importer(importer),
    m_logger(CServiceBroker::GetLogging().GetLogger(
        StringUtils::Format("CAddonMediaImporterExecutor[{}]", addonId)))
{
}

void CAddonMediaImporterExecutor::SetSource(const CMediaImportSource& source)
{
  SetSource(std::make_shared<CMediaImportSource>(source));
}

void CAddonMediaImporterExecutor::SetSource(MediaImportSourcePtr source)
{
  m_source = source;
}

void CAddonMediaImporterExecutor::SetImport(const CMediaImport& import)
{
  SetImport(std::make_shared<CMediaImport>(import));
}

void CAddonMediaImporterExecutor::SetImport(MediaImportPtr import)
{
  m_import = import;

  if (m_import != nullptr && m_source == nullptr)
    SetSource(m_import->GetSource());
}

void CAddonMediaImporterExecutor::SetTask(IMediaImportTask* task)
{
  m_task = task;
}

bool CAddonMediaImporterExecutor::Execute(const std::string& options)
{
  m_currentProgress = 0;
  m_currentTotal = 0;

  return RunScript(options);
}

bool CAddonMediaImporterExecutor::ShouldCancel(unsigned int progress, unsigned int total)
{
  m_currentProgress = progress;
  m_currentTotal = total;

  if (m_task != nullptr)
    return m_task->ShouldCancel(progress, total);

  return false;
}

void CAddonMediaImporterExecutor::SetProgressStatus(const std::string& status)
{
  if (m_task != nullptr)
    return m_task->SetProgressText(status);
}

bool CAddonMediaImporterExecutor::IsCancelled() const
{
  if (m_task != nullptr)
    return m_task->ShouldCancel(m_currentProgress, m_currentTotal);

  return false;
}

std::shared_ptr<ADDON::CMediaImporter> CAddonMediaImporterExecutor::GetAddon(
    const std::string& addonId)
{
  ADDON::AddonPtr addon;
  if (!CServiceBroker::GetAddonMgr().GetAddon(addonId, addon, ADDON::ADDON_MEDIAIMPORTER, true))
    return nullptr;

  return std::static_pointer_cast<ADDON::CMediaImporter>(addon);
}

std::shared_ptr<ADDON::CMediaImporter> CAddonMediaImporterExecutor::GetAddon() const
{
  return GetAddon(m_addonId);
}

CAddonMediaImporterExecutor* CAddonMediaImporterExecutor::GetExecutorFromHandle(
    HandleType handle) throw(InvalidAddonMediaImporterHandleException)
{
  auto importer = GetScriptFromHandle(handle);
  if (importer != nullptr)
    return importer;

  static Logger logger = CServiceBroker::GetLogging().GetLogger("CAddonMediaImporterExecutor");

  logger->error("invalid script handle {:d}", handle);
  throw InvalidAddonMediaImporterHandleException("handle: %d", handle);
}

bool CAddonMediaImporterExecutor::CheckAction(Action action) const
    throw(InvalidAddonMediaImporterCallbackException)
{
  if (m_action == action)
    return true;

  m_logger->warn("callback for unexpected action received");
  throw InvalidAddonMediaImporterCallbackException("add-on ID: %s", m_addonId.c_str());
}

void CAddonMediaImporterExecutor::SetSuccess(bool success, Action action)
{
  if (!CheckAction(action))
    return;

  m_success = success;
}

bool CAddonMediaImporterExecutor::RunScript(const std::string& options)
{
  const auto path = GetScriptPath(options);
  if (path.empty())
    return false;

  auto addon = GetAddon();
  if (addon == nullptr)
  {
    m_logger->error("failed to find media importer add-on");
    return false;
  }

  // reset variables
  m_success = false;

  return CRunningScriptsHandler::RunScript(this, addon, path, false);
}

std::string CAddonMediaImporterExecutor::GetScriptPath(const std::string& options /* = "" */) const
{
  CURL addonPath;
  addonPath.SetProtocol("addons");
  addonPath.SetHostName(m_addonId);
  addonPath.SetFileName(m_actionName);

  if (!options.empty())
    addonPath.SetOptions("?" + options);

  return addonPath.Get();
}

std::string CAddonMediaImporterExecutor::ActionToString(Action action)
{
  switch (action)
  {
    case Action::DiscoverSource:
      return "discoverprovider";
    case Action::LookupSource:
      return "lookupprovider";
    case Action::CanImport:
      return "canimport";
    case Action::IsSourceReady:
      return "isproviderready";
    case Action::IsImportReady:
      return "isimportready";
    case Action::LoadSourceSettings:
      return "loadprovidersettings";
    case Action::UnloadSourceSettings:
      return "loadimportsettings";
    case Action::LoadImportSettings:
      return "loadimportsettings";
    case Action::UnloadImportSettings:
      return "unloadimportsettings";
    case Action::CanUpdateMetadataOnSource:
      return "canupdatemetadataonprovider";
    case Action::CanUpdatePlaycountOnSource:
      return "canupdateplaycountonprovider";
    case Action::CanUpdateLastPlayedOnSource:
      return "canupdatelastplayedonprovider";
    case Action::CanUpdateResumePositionOnSource:
      return "canupdateresumepositiononprovider";
    case Action::Import:
      return "import";
    case Action::UpdateOnSource:
      return "updateonprovider";
    case Action::SettingOptionsFiller:
      return "settingoptionsfiller";
    case Action::SettingActionCallback:
      return "settingactioncallback";

    default:
      break;
  }

  return "unknown";
}
