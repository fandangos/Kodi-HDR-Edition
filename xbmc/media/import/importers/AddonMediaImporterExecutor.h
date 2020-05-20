/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "commons/Exception.h"
#include "interfaces/generic/RunningScriptsHandler.h"
#include "media/import/MediaImport.h"
#include "media/import/MediaImportSource.h"
#include "utils/logtypes.h"

#include <map>
#include <string>

namespace ADDON
{
class CMediaImporter;
}

class CAddonMediaImporter;
class IMediaImportTask;

XBMCCOMMONS_STANDARD_EXCEPTION(InvalidAddonMediaImporterHandleException);
XBMCCOMMONS_STANDARD_EXCEPTION(InvalidAddonMediaImporterCallbackException);

class CAddonMediaImporterExecutor : public CRunningScriptsHandler<CAddonMediaImporterExecutor>
{
public:
  enum class Action
  {
    NoAction,
    DiscoverSource,
    LookupSource,
    CanImport,
    IsSourceReady,
    IsImportReady,
    LoadSourceSettings,
    UnloadSourceSettings,
    LoadImportSettings,
    UnloadImportSettings,
    CanUpdateMetadataOnSource,
    CanUpdatePlaycountOnSource,
    CanUpdateLastPlayedOnSource,
    CanUpdateResumePositionOnSource,
    Import,
    UpdateOnSource,
    SettingOptionsFiller,
    SettingActionCallback,
  };

  CAddonMediaImporterExecutor(const std::string& addonId,
                              Action action,
                              CAddonMediaImporter* importer = nullptr,
                              const std::string& actionName = "");
  virtual ~CAddonMediaImporterExecutor() = default;

  const std::string& GetAddonId() const { return m_addonId; }

  CAddonMediaImporter* GetImporter() const { return m_importer; }
  void SetImporter(CAddonMediaImporter* importer) { m_importer = importer; }

  MediaImportSourcePtr GetSource() const { return m_source; }
  void SetSource(const CMediaImportSource& source);
  void SetSource(MediaImportSourcePtr source);

  MediaImportPtr GetImport() const { return m_import; }
  void SetImport(const CMediaImport& import);
  void SetImport(MediaImportPtr import);

  IMediaImportTask* GetTask() const { return m_task; }
  void SetTask(IMediaImportTask* task);

  bool Execute(const std::string& options);

  bool ShouldCancel(unsigned int progress, unsigned int total);
  void SetProgressStatus(const std::string& status);

protected:
  friend class CAddonMediaImporterDiscoverer;
  friend class CAddonMediaImporter;

  // implementations of CRunningScriptsHandler / CScriptRunner
  bool IsSuccessful() const override { return m_success; }
  bool IsCancelled() const override;

  static std::shared_ptr<ADDON::CMediaImporter> GetAddon(const std::string& addonId);

  static CAddonMediaImporterExecutor* GetExecutorFromHandle(HandleType handle) throw(
      InvalidAddonMediaImporterHandleException);
  bool CheckAction(Action action) const throw(InvalidAddonMediaImporterCallbackException);
  void SetSuccess(bool success, Action action);

private:
  std::shared_ptr<ADDON::CMediaImporter> GetAddon() const;

  bool RunScript(const std::string& options);
  std::string GetScriptPath(const std::string& options = "") const;

  static std::string ActionToString(Action action);

  const std::string m_addonId;
  const Action m_action;
  const std::string m_actionName;
  CAddonMediaImporter* m_importer = nullptr;

  bool m_started = false;
  unsigned int m_currentProgress = 0;
  unsigned int m_currentTotal = 0;
  bool m_success = false;

  MediaImportPtr m_import;
  MediaImportSourcePtr m_source;
  IMediaImportTask* m_task = nullptr;

  Logger m_logger;
};
