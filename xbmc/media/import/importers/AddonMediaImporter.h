/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "addons/settings/IAddonSettingsCallbackExecutor.h"
#include "commons/Exception.h"
#include "interfaces/generic/RunningScriptsHandler.h"
#include "media/import/MediaImportChangesetTypes.h"
#include "media/import/importers/AddonMediaImporterExecutor.h"
#include "media/import/importers/BaseMediaImporter.h"
#include "settings/lib/SettingDefinitions.h"
#include "settings/lib/SettingType.h"
#include "utils/StaticLoggerBase.h"
#include "utils/logtypes.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace ADDON
{
struct AddonEvent;
class CAddonMgr;
class CMediaImporter;
} // namespace ADDON

class CMediaImportManager;

class CAddonMediaImporterBaseInternal
{
public:
  virtual ~CAddonMediaImporterBaseInternal() = default;

  const std::string& GetAddonId() const { return m_addonId; }

protected:
  explicit CAddonMediaImporterBaseInternal(const std::string& addonId, const std::string& name);

  std::shared_ptr<ADDON::CMediaImporter> GetAddon() const;

  const std::string m_addonId;

  Logger m_logger;
};

class CAddonMediaImporterBase : public virtual IMediaImporterBase,
                                public CAddonMediaImporterBaseInternal
{
public:
  ~CAddonMediaImporterBase() override = default;

  // implementations of IMediaImporterBase
  const char* GetIdentification() const override { return m_addonId.c_str(); }
  bool CanLookupSource() const override;
  std::string GetSourceLookupProtocol() const override;

protected:
  explicit CAddonMediaImporterBase(const std::string& addonId, const std::string& name);
};

class CAddonMediaImporterDiscoverer : public IMediaImporterDiscoverer,
                                      public CAddonMediaImporterBase
{
public:
  using HandleType = CAddonMediaImporterExecutor::HandleType;

  explicit CAddonMediaImporterDiscoverer(const std::string& addonId);
  ~CAddonMediaImporterDiscoverer() override;

  // implementations of IMediaImporterDiscoverer
  void Start() override;

private:
  void Stop();

  bool m_started = false;
};

class CAddonMediaImporter : public CAddonMediaImporterBase,
                            public CBaseMediaImporter,
                            public ADDON::IAddonSettingsCallbackExecutor,
                            protected CStaticLoggerBase
{
public:
  using HandleType = CAddonMediaImporterExecutor::HandleType;

  typedef struct SettingsCallbackData
  {
    enum Type
    {
      Source,
      Import,
    } type;
    union
    {
      CMediaImportSource* source;
      CMediaImport* import;
    } data;
  } SettingsCallbackData;

  explicit CAddonMediaImporter(const std::string& addonId);
  ~CAddonMediaImporter() override = default;

  // implementation of ILocalizer
  std::string Localize(uint32_t code) const override;

  // implementations of IMediaImporter::IImporter
  bool DiscoverSource(CMediaImportSource& source) override;
  bool LookupSource(const CMediaImportSource& source) override;

  bool CanImport(const std::string& path) override;
  bool IsSourceReady(CMediaImportSource& source) override;
  bool IsImportReady(CMediaImport& import) override;
  bool LoadSourceSettings(CMediaImportSource& source) override;
  bool LoadImportSettings(CMediaImport& import) override;
  bool CanUpdateMetadataOnSource(const std::string& path) override;
  bool CanUpdatePlaycountOnSource(const std::string& path) override;
  bool CanUpdateLastPlayedOnSource(const std::string& path) override;
  bool CanUpdateResumePositionOnSource(const std::string& path) override;

  bool Import(CMediaImportImportItemsRetrievalTask* task) override;
  bool UpdateOnSource(CMediaImportUpdateTask* task) override;

  // implementations of IAddonSettingsCallbackExecutor
  bool LoadSettings(std::shared_ptr<CSettingsBase> settings, void* data) override;
  bool SaveSettings(std::shared_ptr<CSettingsBase> settings, void* data) override;
  void SetSettingsLoaded(void* data) override;

  void OnSettingAction(std::shared_ptr<const CSetting> setting,
                       const std::string& callback,
                       void* data) override;

  void OnIntegerSettingOptionsFiller(std::shared_ptr<const CSetting> setting,
                                     const std::string& callback,
                                     IntegerSettingOptions& list,
                                     int& current,
                                     void* data) override;
  bool SetIntegerSettingOptions(const std::string& settingId,
                                const IntegerSettingOptions& list,
                                void* data) override;

  void OnStringSettingOptionsFiller(std::shared_ptr<const CSetting> setting,
                                    const std::string& callback,
                                    StringSettingOptions& list,
                                    std::string& current,
                                    void* data) override;
  bool SetStringSettingOptions(const std::string& settingId,
                               const StringSettingOptions& list,
                               void* data) override;
  ;

  // helper methods for add-on scripts
  static bool PrepareProviderSettings(const std::string& addonId,
                                      MediaImportSettingsBasePtr settings);
  static bool PrepareProviderSettings(std::shared_ptr<ADDON::CMediaImporter> addon,
                                      MediaImportSettingsBasePtr settings);
  static bool PrepareImportSettings(const std::string& addonId,
                                    MediaImportSettingsBasePtr settings);
  static bool PrepareImportSettings(std::shared_ptr<ADDON::CMediaImporter> addon,
                                    MediaImportSettingsBasePtr settings);

  // methods to interact with the add-on script(s)
  static std::string GetImporterId(const std::string& addonId);

  static void SetDiscoveredProviderDetails(
      HandleType handle,
      bool providerDiscovered,
      MediaImportSourcePtr discoveredSource) throw(InvalidAddonMediaImporterHandleException);
  static void SetProviderFound(HandleType handle, bool providerFound);

  static void SetCanImport(HandleType handle,
                           bool canImport) throw(InvalidAddonMediaImporterHandleException);
  static void SetProviderReady(HandleType handle,
                               bool sourceReady) throw(InvalidAddonMediaImporterHandleException);
  static void SetImportReady(HandleType handle,
                             bool importReady) throw(InvalidAddonMediaImporterHandleException);
  static void SetCanUpdateMetadataOnProvider(
      HandleType handle,
      bool canUpdateMetadataOnSource) throw(InvalidAddonMediaImporterHandleException);
  static void SetCanUpdatePlaycountOnProvider(
      HandleType handle,
      bool canUpdatePlaycountOnSource) throw(InvalidAddonMediaImporterHandleException);
  static void SetCanUpdateLastPlayedOnProvider(
      HandleType handle,
      bool canUpdateLastPlayedOnSource) throw(InvalidAddonMediaImporterHandleException);
  static void SetCanUpdateResumePositionOnProvider(
      HandleType handle,
      bool canUpdateResumePositionOnSource) throw(InvalidAddonMediaImporterHandleException);

  // methods concerning the current state and progress of the add-on script(s)
  static bool ShouldCancel(HandleType handle,
                           unsigned int progress,
                           unsigned int total) throw(InvalidAddonMediaImporterHandleException);
  static void SetProgressStatus(HandleType handle, const std::string& status) throw(
      InvalidAddonMediaImporterHandleException);

  // general methods to interact with the add-on script(s)
  static CAddonMediaImporter* GetImporter(HandleType handle) throw(
      InvalidAddonMediaImporterHandleException);
  static MediaImportSourcePtr GetMediaProvider(HandleType handle) throw(
      InvalidAddonMediaImporterHandleException);
  static MediaImportPtr GetMediaImport(HandleType handle) throw(
      InvalidAddonMediaImporterHandleException);

  // methods to interact with the add-on script(s) executing Import()
  static std::vector<CFileItemPtr> GetImportedItems(
      HandleType handle,
      const MediaType& mediaType) throw(InvalidAddonMediaImporterHandleException);
  static void AddImportItem(
      HandleType handle,
      const CFileItemPtr& item,
      const MediaType& mediaType,
      MediaImportChangesetType changesetType =
          MediaImportChangesetType::None) throw(InvalidAddonMediaImporterHandleException);
  static void AddImportItems(
      HandleType handle,
      const std::vector<CFileItemPtr>& items,
      const MediaType& mediaType,
      MediaImportChangesetType changesetType =
          MediaImportChangesetType::None) throw(InvalidAddonMediaImporterHandleException);
  static void FinishImport(HandleType handle, bool isChangeset = false) throw(
      InvalidAddonMediaImporterHandleException);

  // methods to interact with the add-on script(s) executing UpdateOnSource()
  static CFileItemPtr GetUpdatedItem(HandleType handle) throw(
      InvalidAddonMediaImporterHandleException);
  static void FinishUpdateOnProvider(HandleType handle) throw(
      InvalidAddonMediaImporterHandleException);

private:
  bool SetSourceOrImportOnExecutor(CAddonMediaImporterExecutor& executor,
                                   const SettingsCallbackData& callbackData,
                                   const std::string& callback,
                                   std::shared_ptr<const CSetting> setting) const;

  static bool LoadSettingsFromFile(const std::string& addonId,
                                   MediaImportSettingsBasePtr settings,
                                   const std::string& settingDefinitionsFile);

  static void SetSuccess(
      HandleType handle,
      bool success,
      CAddonMediaImporterExecutor::Action action) throw(InvalidAddonMediaImporterHandleException);

  bool m_settingsLoaded = false;

  bool m_integerSettingOptionsSet = false;
  IntegerSettingOptions m_integerSettingOptions;
  bool m_stringSettingOptionsSet = false;
  StringSettingOptions m_stringSettingOptions;
};

class CAddonMediaImporterObserver : public IMediaImporterObserver,
                                    public CAddonMediaImporterBaseInternal
{
public:
  explicit CAddonMediaImporterObserver(const std::string& addonId);
  ~CAddonMediaImporterObserver() override;

  // implementations of IMediaImporterObserver
  void Start() override;
  void OnSourceAdded(const CMediaImportSource& source) override;
  void OnSourceUpdated(const CMediaImportSource& source) override;
  void OnSourceRemoved(const CMediaImportSource& source) override;
  void OnSourceActivated(const CMediaImportSource& source) override;
  void OnSourceDeactivated(const CMediaImportSource& source) override;
  void OnImportAdded(const CMediaImport& import) override;
  void OnImportUpdated(const CMediaImport& import) override;
  void OnImportRemoved(const CMediaImport& import) override;

private:
  void Stop();

  bool m_started = false;
};

class CAddonMediaImporterFactory : public IMediaImporterFactory,
                                   protected CAddonMediaImporterBaseInternal
{
public:
  CAddonMediaImporterFactory(const std::string& addonId);
  ~CAddonMediaImporterFactory() override = default;

  // implementations of IMediaImporterFactory
  const char* GetIdentification() const override { return m_addonId.c_str(); }

  std::unique_ptr<IMediaImporterDiscoverer> CreateDiscoverer() const override;
  std::unique_ptr<IMediaImporter> CreateImporter() const override;
  std::unique_ptr<IMediaImporterObserver> CreateObserver() const override;
};
