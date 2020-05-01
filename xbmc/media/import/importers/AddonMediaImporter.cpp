/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AddonMediaImporter.h"

#include "ServiceBroker.h"
#include "URL.h"
#include "addons/AddonManager.h"
#include "addons/MediaImporter.h"
#include "filesystem/File.h"
#include "guilib/LocalizeStrings.h"
#ifdef HAS_PYTHON
#include "interfaces/python/XBPython.h"
#endif
#include "media/import/MediaImportManager.h"
#include "media/import/MediaImportSource.h"
#include "media/import/importers/AddonMediaImporterExecutor.h"
#include "media/import/jobs/tasks/MediaImportImportItemsRetrievalTask.h"
#include "media/import/jobs/tasks/MediaImportUpdateTask.h"
#include "settings/lib/Setting.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

#include <fmt/format.h>

template<typename TType>
void NullDeleter(TType*)
{
}

template<typename TType>
std::shared_ptr<TType> createSharedPtrWithoutDeleter(TType* obj)
{
  return std::shared_ptr<TType>(obj, &NullDeleter<TType>);
}

SettingType GetSettingType(SettingConstPtr setting)
{
  SettingType settingType = setting->GetType();
  if (settingType == SettingType::List)
    settingType = std::static_pointer_cast<const CSettingList>(setting)->GetDefinition()->GetType();

  return settingType;
}

std::shared_ptr<ADDON::CMediaImporter> GetAddon(const std::string& addonId)
{
  ADDON::AddonPtr addon;
  if (!CServiceBroker::GetAddonMgr().GetAddon(addonId, addon, ADDON::ADDON_MEDIAIMPORTER, true))
    return nullptr;

  return std::static_pointer_cast<ADDON::CMediaImporter>(addon);
}

CAddonMediaImporterBaseInternal::CAddonMediaImporterBaseInternal(const std::string& addonId,
                                                                 const std::string& name)
  : m_addonId(addonId),
    m_logger(CServiceBroker::GetLogging().GetLogger(StringUtils::Format("{}[{}]", name, addonId)))
{
}

std::shared_ptr<ADDON::CMediaImporter> CAddonMediaImporterBaseInternal::GetAddon() const
{
  return ::GetAddon(m_addonId);
}

CAddonMediaImporterBase::CAddonMediaImporterBase(const std::string& addonId,
                                                 const std::string& name)
  : CAddonMediaImporterBaseInternal(addonId, name)
{
}

bool CAddonMediaImporterBase::CanLookupSource() const
{
  const auto addon = GetAddon();
  if (addon == nullptr)
    return false;

  return addon->CanLookupProvider();
}

std::string CAddonMediaImporterBase::GetSourceLookupProtocol() const
{
  const auto addon = GetAddon();
  if (addon == nullptr)
    return "";

  return addon->ProviderLookupProtocol();
}

CAddonMediaImporterDiscoverer::CAddonMediaImporterDiscoverer(const std::string& addonId)
  : CAddonMediaImporterBase(addonId, "CAddonMediaImporterDiscoverer")
{
}

CAddonMediaImporterDiscoverer::~CAddonMediaImporterDiscoverer()
{
  Stop();
}

void CAddonMediaImporterDiscoverer::Start()
{
  auto mediaImporter = GetAddon();
  if (mediaImporter == nullptr)
  {
    m_logger->warn("cannot start due to missing add-on");
    return;
  }

  if (mediaImporter->AutomaticallyAddAsProvider())
  {
    // automatically add the add-on as a provider
    const auto& id = mediaImporter->ID();
    CMediaImportSource source(id, "plugin://" + id, mediaImporter->Name(), mediaImporter->Icon(),
                              mediaImporter->SupportedMediaTypes());
    source.SetImporterId(id);

    if (CServiceBroker::GetMediaImportManager().AddAndActivateSource(source))
      m_logger->info("source automatically added");
    else
      m_logger->warn("failed to automatically add source");
    return;
  }
  else
  {
    // check if the media importer add-on has a discovery service
    if (!mediaImporter->HasDiscoveryService())
      return;

    // try to start the discovery service
    if (!CServiceBroker::GetMediaImportAddons().StartDiscovery(mediaImporter))
    {
      m_logger->warn("failed to start");
      return;
    }

    m_logger->debug("successfully started");
  }
  m_started = true;
}

void CAddonMediaImporterDiscoverer::Stop()
{
  if (!m_started)
    return;

  auto mediaImporter = GetAddon();
  if (mediaImporter == nullptr)
  {
    m_logger->warn("cannot stop due to missing add-on");
    return;
  }

  // deactivate automatically added provider
  if (mediaImporter->AutomaticallyAddAsProvider())
  {
    CServiceBroker::GetMediaImportManager().DeactivateSource(mediaImporter->ID());
    m_logger->debug("automatically added provider deactivated");
    return;
  }

  // check if the media importer add-on has a discovery service
  if (!mediaImporter->HasDiscoveryService())
    return;

  // try to stop the discovery service
  if (!CServiceBroker::GetMediaImportAddons().StopDiscovery(mediaImporter))
  {
    m_logger->warn("failed to stop");
    return;
  }

  m_logger->debug("successfully stopped");
}

CAddonMediaImporter::CAddonMediaImporter(const std::string& addonId)
  : CAddonMediaImporterBase(addonId, "CAddonMediaImporter"),
    CStaticLoggerBase("CAddonMediaImporter")
{
}

std::string CAddonMediaImporter::Localize(uint32_t code) const
{
  const auto addon = GetAddon();
  if (addon == nullptr)
    return "";

  return g_localizeStrings.GetAddonString(addon->ID(), code);
}

bool CAddonMediaImporter::DiscoverSource(CMediaImportSource& source)
{
  if (!CAddonMediaImporterBase::CanLookupSource())
    return false;

  CAddonMediaImporterExecutor executor(m_addonId,
                                       CAddonMediaImporterExecutor::Action::DiscoverSource, this);
  executor.SetSource(createSharedPtrWithoutDeleter(&source));
  return executor.Execute("");
}

bool CAddonMediaImporter::LookupSource(const CMediaImportSource& source)
{
  if (source.GetIdentifier().empty() || source.GetBasePath().empty() ||
      !CAddonMediaImporterBase::CanLookupSource())
    return false;

  CAddonMediaImporterExecutor executor(m_addonId, CAddonMediaImporterExecutor::Action::LookupSource,
                                       this);
  executor.SetSource(source);
  return executor.Execute("");
}

bool CAddonMediaImporter::CanImport(const std::string& path)
{
  const auto importer = GetAddon();
  if (importer == nullptr)
    return false;

  // no need to check automatically added providers
  if (importer->AutomaticallyAddAsProvider())
    return true;

  CUrlOptions urlOptions;
  urlOptions.AddOption("path", path);

  CAddonMediaImporterExecutor executor(m_addonId, CAddonMediaImporterExecutor::Action::CanImport,
                                       this);
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::IsSourceReady(CMediaImportSource& source)
{
  CUrlOptions urlOptions;
  urlOptions.AddOption("provider", source.GetIdentifier());

  CAddonMediaImporterExecutor executor(m_addonId,
                                       CAddonMediaImporterExecutor::Action::IsSourceReady, this);
  executor.SetSource(createSharedPtrWithoutDeleter(&source));
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::IsImportReady(CMediaImport& import)
{
  CUrlOptions urlOptions;
  urlOptions.AddOption("path", import.GetPath());
  urlOptions.AddOption("mediatypes", import.GetMediaTypes());

  CAddonMediaImporterExecutor executor(m_addonId,
                                       CAddonMediaImporterExecutor::Action::IsImportReady, this);
  executor.SetSource(createSharedPtrWithoutDeleter(&import.GetSource()));
  executor.SetImport(createSharedPtrWithoutDeleter(&import));
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::LoadSourceSettings(CMediaImportSource& source)
{
  const auto importer = GetAddon();

  // nothing to do if there is no specific settings XML file
  if (importer->ProviderSettingsPath().empty())
    return true;

  // prepare the provider settings
  if (!PrepareProviderSettings(importer, source.Settings()))
    return false;

  if (!importer->PrepareProviderSettings())
    return true;

  m_settingsLoaded = false;

  CUrlOptions urlOptions;
  urlOptions.AddOption("provider", source.GetIdentifier());

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::LoadSourceSettings, this);
  executor.SetSource(createSharedPtrWithoutDeleter(&source));
  executor.Execute(urlOptions.GetOptionsString());

  return m_settingsLoaded;
}

bool CAddonMediaImporter::LoadImportSettings(CMediaImport& import)
{
  const auto importer = GetAddon();

  // nothing to do if there is no specific settings XML file
  if (importer->ImportSettingsPath().empty())
    return true;

  // prepare the import settings
  if (!PrepareImportSettings(importer, import.Settings()))
    return false;

  if (!importer->PrepareImportSettings())
    return true;

  m_settingsLoaded = false;

  CUrlOptions urlOptions;
  urlOptions.AddOption("path", import.GetPath());
  urlOptions.AddOption("mediatypes", import.GetMediaTypes());

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::LoadImportSettings, this);
  executor.SetSource(createSharedPtrWithoutDeleter(&import.GetSource()));
  executor.SetImport(createSharedPtrWithoutDeleter(&import));
  executor.Execute(urlOptions.GetOptionsString());

  return m_settingsLoaded;
}

bool CAddonMediaImporter::CanUpdateMetadataOnSource(const std::string& path)
{
  CUrlOptions urlOptions;
  urlOptions.AddOption("path", path);

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::CanUpdateMetadataOnSource, this);
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::CanUpdatePlaycountOnSource(const std::string& path)
{
  CUrlOptions urlOptions;
  urlOptions.AddOption("path", path);

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::CanUpdatePlaycountOnSource, this);
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::CanUpdateLastPlayedOnSource(const std::string& path)
{
  CUrlOptions urlOptions;
  urlOptions.AddOption("path", path);

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::CanUpdateLastPlayedOnSource, this);
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::CanUpdateResumePositionOnSource(const std::string& path)
{
  CUrlOptions urlOptions;
  urlOptions.AddOption("path", path);

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::CanUpdateResumePositionOnSource, this);
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::Import(CMediaImportImportItemsRetrievalTask* task)
{
  if (task == nullptr)
    return false;

  auto import = task->GetImport();

  CUrlOptions urlOptions;
  urlOptions.AddOption("path", import.GetPath());
  urlOptions.AddOption("mediatypes", import.GetMediaTypes());

  CAddonMediaImporterExecutor executor(m_addonId, CAddonMediaImporterExecutor::Action::Import,
                                       this);
  executor.SetSource(createSharedPtrWithoutDeleter(&import.GetSource()));
  executor.SetImport(createSharedPtrWithoutDeleter(&import));
  executor.SetTask(task);
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::UpdateOnSource(CMediaImportUpdateTask* task)
{
  if (task == nullptr)
    return false;

  auto import = task->GetImport();

  CUrlOptions urlOptions;
  urlOptions.AddOption("provider", import.GetSource().GetIdentifier());
  urlOptions.AddOption("path", import.GetPath());
  urlOptions.AddOption("mediatypes", import.GetMediaTypes());

  CAddonMediaImporterExecutor executor(m_addonId,
                                       CAddonMediaImporterExecutor::Action::UpdateOnSource, this);
  executor.SetSource(createSharedPtrWithoutDeleter(&import.GetSource()));
  executor.SetImport(createSharedPtrWithoutDeleter(&import));
  executor.SetTask(task);
  return executor.Execute(urlOptions.GetOptionsString());
}

bool CAddonMediaImporter::LoadSettings(std::shared_ptr<CSettingsBase> settings, void* data)
{
  if (settings == nullptr)
    return false;

  if (data == nullptr)
  {
    m_logger->warn("missing data to load settings");
    return false;
  }

  auto callbackData = static_cast<SettingsCallbackData*>(data);
  if (callbackData->type == SettingsCallbackData::Source)
  {
    if (callbackData->data.source == nullptr)
    {
      m_logger->warn("missing source to load settings");
      return false;
    }

    if (!PrepareProviderSettings(m_addonId, callbackData->data.source->Settings()))
      return false;
  }
  else if (callbackData->type == SettingsCallbackData::Import)
  {
    if (callbackData->data.import == nullptr)
    {
      m_logger->warn("missing import to load settings");
      return false;
    }

    if (!PrepareImportSettings(m_addonId, callbackData->data.import->Settings()))
      return false;
  }
  else
  {
    m_logger->warn("invalid data to load settings");
    return false;
  }

  return true;
}

bool CAddonMediaImporter::SaveSettings(std::shared_ptr<CSettingsBase> settings, void* data)
{
  if (settings == nullptr)
    return false;

  return settings->Save();
}

void CAddonMediaImporter::SetSettingsLoaded(void* data)
{
  m_settingsLoaded = true;
}

void CAddonMediaImporter::OnSettingAction(std::shared_ptr<const CSetting> setting,
                                          const std::string& callback,
                                          void* data)
{
  if (setting == nullptr || callback.empty())
    return;

  if (data == nullptr)
  {
    m_logger->warn("missing data to handle action setting callback \"{}\" for setting \"{}\"",
                   callback, setting->GetId());
    return;
  }

  auto callbackData = static_cast<SettingsCallbackData*>(data);

  CUrlOptions urlOptions;
  urlOptions.AddOption("setting", setting->GetId());

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::SettingActionCallback, this, callback);
  if (!SetSourceOrImportOnExecutor(executor, *callbackData, callback, setting))
    return;

  executor.Execute(urlOptions.GetOptionsString());
}

void CAddonMediaImporter::OnIntegerSettingOptionsFiller(std::shared_ptr<const CSetting> setting,
                                                        const std::string& callback,
                                                        IntegerSettingOptions& list,
                                                        int& current,
                                                        void* data)
{
  if (setting == nullptr || callback.empty())
    return;

  if (data == nullptr)
  {
    m_logger->warn("missing data to handle integer setting options filler "
                   "callback \"{}\" for setting \"{}\"",
                   callback, setting->GetId());
    return;
  }

  auto callbackData = static_cast<SettingsCallbackData*>(data);

  // clear the setting options
  m_integerSettingOptionsSet = false;
  m_integerSettingOptions.clear();

  CUrlOptions urlOptions;
  urlOptions.AddOption("setting", setting->GetId());

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::SettingOptionsFiller, this, callback);
  if (!SetSourceOrImportOnExecutor(executor, *callbackData, callback, setting))
    return;

  executor.Execute(urlOptions.GetOptionsString());

  if (!m_integerSettingOptionsSet)
  {
    m_logger->error("integer setting options filler callback \"{}\" for setting \"{}\" failed",
                    callback, setting->GetId());
    return;
  }

  list = m_integerSettingOptions;

  // validate the current value
  bool foundCurrent =
      std::any_of(list.cbegin(), list.cend(), [&current](const IntegerSettingOption& option) {
        return option.value == current;
      });

  // reset the current value to the default
  if (!foundCurrent)
    current = std::static_pointer_cast<const CSettingInt>(setting)->GetDefault();
}

bool CAddonMediaImporter::SetIntegerSettingOptions(const std::string& settingId,
                                                   const IntegerSettingOptions& list,
                                                   void* data)
{
  m_integerSettingOptions = list;
  m_integerSettingOptionsSet = true;
  return true;
}

void CAddonMediaImporter::OnStringSettingOptionsFiller(std::shared_ptr<const CSetting> setting,
                                                       const std::string& callback,
                                                       StringSettingOptions& list,
                                                       std::string& current,
                                                       void* data)
{
  if (setting == nullptr || callback.empty())
    return;

  if (data == nullptr)
  {
    m_logger->warn("missing data to handle string setting options filler "
                   "callback \"{}\" for setting \"{}\"",
                   callback, setting->GetId());
    return;
  }

  auto callbackData = static_cast<SettingsCallbackData*>(data);

  // clear the setting options
  m_stringSettingOptionsSet = false;
  m_stringSettingOptions.clear();

  CUrlOptions urlOptions;
  urlOptions.AddOption("setting", setting->GetId());

  CAddonMediaImporterExecutor executor(
      m_addonId, CAddonMediaImporterExecutor::Action::SettingOptionsFiller, this, callback);
  if (!SetSourceOrImportOnExecutor(executor, *callbackData, callback, setting))
    return;

  executor.Execute(urlOptions.GetOptionsString());

  if (!m_stringSettingOptionsSet)
  {
    m_logger->error("string setting options filler callback \"{}\" for setting \"{}\" failed",
                    callback, setting->GetId());
    return;
  }

  list = m_stringSettingOptions;

  // validate the current value
  bool foundCurrent =
      std::any_of(list.cbegin(), list.cend(), [&current](const StringSettingOption& option) {
        return option.value == current;
      });

  // reset the current value to the default
  if (!foundCurrent)
    current = std::static_pointer_cast<const CSettingString>(setting)->GetDefault();
}

bool CAddonMediaImporter::SetStringSettingOptions(const std::string& settingId,
                                                  const StringSettingOptions& list,
                                                  void* data)
{
  m_stringSettingOptions = list;
  m_stringSettingOptionsSet = true;
  return true;
}

bool CAddonMediaImporter::PrepareProviderSettings(const std::string& addonId,
                                                  MediaImportSettingsBasePtr settings)
{
  if (addonId.empty() || settings == nullptr)
    return false;

  // check if the media import add-on provides an XML file with the setting
  // definitions for the media provider and load it
  const auto importer = ::GetAddon(addonId);
  if (importer == nullptr)
    return false;

  return PrepareProviderSettings(importer, settings);
}

bool CAddonMediaImporter::PrepareProviderSettings(std::shared_ptr<ADDON::CMediaImporter> addon,
                                                  MediaImportSettingsBasePtr settings)
{
  // nothing to do if there is no specific settings XML file
  if (addon->ProviderSettingsPath().empty())
    return true;

  if (LoadSettingsFromFile(addon->ID(), settings, addon->ProviderSettingsPath()))
    return true;

  s_logger->warn("failed to load provider settings for {} from {}", addon->ID(),
                 addon->ProviderSettingsPath());

  return false;
}

bool CAddonMediaImporter::PrepareImportSettings(const std::string& addonId,
                                                MediaImportSettingsBasePtr settings)
{
  // check if the media import add-on provides an XML file with the setting
  // definitions for the media provider and load it
  const auto importer = ::GetAddon(addonId);
  if (importer == nullptr)
    return false;

  return PrepareImportSettings(importer, settings);
}

bool CAddonMediaImporter::PrepareImportSettings(std::shared_ptr<ADDON::CMediaImporter> addon,
                                                MediaImportSettingsBasePtr settings)
{
  // nothing to do if there is no specific settings XML file
  if (addon->ImportSettingsPath().empty())
    return true;

  if (LoadSettingsFromFile(addon->ID(), settings, addon->ImportSettingsPath()))
    return true;

  s_logger->warn("failed to load import settings for {} from {}", addon->ID(),
                 addon->ImportSettingsPath());

  return false;
}

std::string CAddonMediaImporter::GetImporterId(const std::string& addonId)
{
  const auto importerAddon = ::GetAddon(addonId);
  if (importerAddon == nullptr)
    return "";

  return importerAddon->ID();
}

void CAddonMediaImporter::SetDiscoveredProviderDetails(
    HandleType handle,
    bool providerDiscovered,
    MediaImportSourcePtr discoveredSource) throw(InvalidAddonMediaImporterHandleException)
{
  if (discoveredSource == nullptr)
    providerDiscovered = false;

  auto executor = CAddonMediaImporterExecutor::GetExecutorFromHandle(handle);
  executor->SetSuccess(providerDiscovered, CAddonMediaImporterExecutor::Action::DiscoverSource);

  if (discoveredSource != nullptr)
    *executor->GetSource() = *discoveredSource;
}

void CAddonMediaImporter::SetProviderFound(HandleType handle, bool providerFound)
{
  auto executor = CAddonMediaImporterExecutor::GetExecutorFromHandle(handle);
  executor->SetSuccess(providerFound, CAddonMediaImporterExecutor::Action::LookupSource);
}

bool CAddonMediaImporter::SetSourceOrImportOnExecutor(CAddonMediaImporterExecutor& executor,
                                                      const SettingsCallbackData& callbackData,
                                                      const std::string& callback,
                                                      std::shared_ptr<const CSetting> setting) const
{
  if (callbackData.type == SettingsCallbackData::Source)
  {
    if (callbackData.data.source == nullptr)
    {
      m_logger->warn("missing source to handle setting callback \"{}\" for setting \"{}\"",
                     callback, setting->GetId());
      return false;
    }
    executor.SetSource(*static_cast<CMediaImportSource*>(callbackData.data.source));
  }
  else if (callbackData.type == SettingsCallbackData::Import)
  {
    if (callbackData.data.import == nullptr)
    {
      m_logger->warn("missing import to handle setting callback \"{}\" for setting \"{}\"",
                     callback, setting->GetId());
      return false;
    }
    executor.SetImport(*static_cast<CMediaImport*>(callbackData.data.import));
  }
  else
  {
    m_logger->warn("invalid data to handle setting callback \"{}\" for setting \"{}\"", callback,
                   setting->GetId());
    return false;
  }

  return true;
}

bool CAddonMediaImporter::LoadSettingsFromFile(const std::string& addonId,
                                               MediaImportSettingsBasePtr settings,
                                               const std::string& settingDefinitionsFile)
{
  if (settings == nullptr)
    return false;

  CXBMCTinyXML xmlDoc;
  if (!XFILE::CFile::Exists(settingDefinitionsFile))
  {
    s_logger->error("could not find provider settings for {} at {}", addonId,
                    settingDefinitionsFile);
    return false;
  }
  if (!xmlDoc.LoadFile(settingDefinitionsFile))
  {
    s_logger->error("failed to parse provider settings for {} from {}: Line {}, {}", addonId,
                    settingDefinitionsFile, xmlDoc.ErrorRow(), xmlDoc.ErrorDesc());
    return false;
  }
  if (xmlDoc.RootElement() == nullptr)
    return false;

  TiXmlPrinter printer;
  xmlDoc.Accept(&printer);

  const auto settingDefinition = printer.Str();

  // check if the setting definition has already been loaded
  if (settings->IsLoaded())
  {
    if (settings->HasDefinition(settingDefinition))
      return true;

    // otherwise unload the settings
    settings->Unload();
  }

  s_logger->debug("loading settings for {} from {}...", addonId, settingDefinitionsFile);

  settings->AddDefinition(settingDefinition);
  return settings->Load();
}

void CAddonMediaImporter::SetCanImport(HandleType handle, bool canImport) throw(
    InvalidAddonMediaImporterHandleException)
{
  SetSuccess(handle, canImport, CAddonMediaImporterExecutor::Action::CanImport);
}

void CAddonMediaImporter::SetProviderReady(HandleType handle, bool sourceReady) throw(
    InvalidAddonMediaImporterHandleException)
{
  SetSuccess(handle, sourceReady, CAddonMediaImporterExecutor::Action::IsSourceReady);
}

void CAddonMediaImporter::SetImportReady(HandleType handle, bool importReady) throw(
    InvalidAddonMediaImporterHandleException)
{
  SetSuccess(handle, importReady, CAddonMediaImporterExecutor::Action::IsImportReady);
}

void CAddonMediaImporter::SetCanUpdateMetadataOnProvider(
    HandleType handle,
    bool canUpdateMetadataOnSource) throw(InvalidAddonMediaImporterHandleException)
{
  SetSuccess(handle, canUpdateMetadataOnSource,
             CAddonMediaImporterExecutor::Action::CanUpdateMetadataOnSource);
}

void CAddonMediaImporter::SetCanUpdatePlaycountOnProvider(
    HandleType handle,
    bool canUpdatePlaycountOnSource) throw(InvalidAddonMediaImporterHandleException)
{
  SetSuccess(handle, canUpdatePlaycountOnSource,
             CAddonMediaImporterExecutor::Action::CanUpdatePlaycountOnSource);
}

void CAddonMediaImporter::SetCanUpdateLastPlayedOnProvider(
    HandleType handle,
    bool canUpdateLastPlayedOnSource) throw(InvalidAddonMediaImporterHandleException)
{
  SetSuccess(handle, canUpdateLastPlayedOnSource,
             CAddonMediaImporterExecutor::Action::CanUpdateLastPlayedOnSource);
}

void CAddonMediaImporter::SetCanUpdateResumePositionOnProvider(
    HandleType handle,
    bool canUpdateResumePositionOnSource) throw(InvalidAddonMediaImporterHandleException)
{
  SetSuccess(handle, canUpdateResumePositionOnSource,
             CAddonMediaImporterExecutor::Action::CanUpdateResumePositionOnSource);
}

bool CAddonMediaImporter::ShouldCancel(
    HandleType handle,
    unsigned int progress,
    unsigned int total) throw(InvalidAddonMediaImporterHandleException)
{
  return CAddonMediaImporterExecutor::GetExecutorFromHandle(handle)->ShouldCancel(progress, total);
}

void CAddonMediaImporter::SetProgressStatus(HandleType handle, const std::string& status) throw(
    InvalidAddonMediaImporterHandleException)
{
  CAddonMediaImporterExecutor::GetExecutorFromHandle(handle)->SetProgressStatus(status);
}

CAddonMediaImporter* CAddonMediaImporter::GetImporter(HandleType handle) throw(
    InvalidAddonMediaImporterHandleException)
{
  return CAddonMediaImporterExecutor::GetExecutorFromHandle(handle)->GetImporter();
}

MediaImportSourcePtr CAddonMediaImporter::GetMediaProvider(HandleType handle) throw(
    InvalidAddonMediaImporterHandleException)
{
  return CAddonMediaImporterExecutor::GetExecutorFromHandle(handle)->GetSource();
}

MediaImportPtr CAddonMediaImporter::GetMediaImport(HandleType handle) throw(
    InvalidAddonMediaImporterHandleException)
{
  return CAddonMediaImporterExecutor::GetExecutorFromHandle(handle)->GetImport();
}

std::vector<CFileItemPtr> CAddonMediaImporter::GetImportedItems(
    HandleType handle, const MediaType& mediaType) throw(InvalidAddonMediaImporterHandleException)
{
  auto executor = CAddonMediaImporterExecutor::GetExecutorFromHandle(handle);
  if (!executor->CheckAction(CAddonMediaImporterExecutor::Action::Import) ||
      executor->GetTask() == nullptr)
  {
    s_logger->error("cannot get imported items for handle {}", handle);
    return {};
  }

  auto retrievalTask = dynamic_cast<CMediaImportImportItemsRetrievalTask*>(executor->GetTask());
  if (retrievalTask == nullptr)
  {
    s_logger->error("invalid import task ({}) to get imported items for handle {}",
                    static_cast<int>(executor->GetTask()->GetType()), handle);
    return {};
  }

  return retrievalTask->GetLocalItems(mediaType);
}

void CAddonMediaImporter::AddImportItem(
    HandleType handle,
    const CFileItemPtr& item,
    const MediaType& mediaType,
    MediaImportChangesetType
        changesetType /* = MediaImportChangesetTypeNone */) throw(InvalidAddonMediaImporterHandleException)
{
  auto executor = CAddonMediaImporterExecutor::GetExecutorFromHandle(handle);
  if (!executor->CheckAction(CAddonMediaImporterExecutor::Action::Import) ||
      executor->GetTask() == nullptr)
  {
    s_logger->error("cannot add imported item for handle {}", handle);
    return;
  }

  auto retrievalTask = dynamic_cast<CMediaImportImportItemsRetrievalTask*>(executor->GetTask());
  if (retrievalTask == nullptr)
  {
    s_logger->error("invalid import task ({}) to add imported item for handle {}",
                    static_cast<int>(executor->GetTask()->GetType()), handle);
    return;
  }

  retrievalTask->AddItem(item, mediaType, changesetType);
}

void CAddonMediaImporter::AddImportItems(
    HandleType handle,
    const std::vector<CFileItemPtr>& items,
    const MediaType& mediaType,
    MediaImportChangesetType
        changesetType /* = MediaImportChangesetTypeNone */) throw(InvalidAddonMediaImporterHandleException)
{
  auto executor = CAddonMediaImporterExecutor::GetExecutorFromHandle(handle);
  if (!executor->CheckAction(CAddonMediaImporterExecutor::Action::Import) ||
      executor->GetTask() == nullptr)
  {
    s_logger->error("cannot add imported items for handle {}", handle);
    return;
  }

  auto retrievalTask = dynamic_cast<CMediaImportImportItemsRetrievalTask*>(executor->GetTask());
  if (retrievalTask == nullptr)
  {
    s_logger->error("invalid import task ({}) to add imported items for handle {}",
                    static_cast<int>(executor->GetTask()->GetType()), handle);
    return;
  }

  retrievalTask->AddItems(items, mediaType, changesetType);
}

void CAddonMediaImporter::FinishImport(HandleType handle, bool isChangeset /* = false */) throw(
    InvalidAddonMediaImporterHandleException)
{
  auto executor = CAddonMediaImporterExecutor::GetExecutorFromHandle(handle);
  if (!executor->CheckAction(CAddonMediaImporterExecutor::Action::Import) ||
      executor->GetTask() == nullptr)
  {
    s_logger->error("cannot finish import for handle {}", handle);
    return;
  }

  auto retrievalTask = dynamic_cast<CMediaImportImportItemsRetrievalTask*>(executor->GetTask());
  if (retrievalTask == nullptr)
  {
    s_logger->error("invalid import task ({}) to finish import for handle {}",
                    static_cast<int>(executor->GetTask()->GetType()), handle);
    return;
  }

  retrievalTask->SetChangeset(isChangeset);

  executor->SetSuccess(true, CAddonMediaImporterExecutor::Action::Import);
}

CFileItemPtr CAddonMediaImporter::GetUpdatedItem(HandleType handle) throw(
    InvalidAddonMediaImporterHandleException)
{
  auto executor = CAddonMediaImporterExecutor::GetExecutorFromHandle(handle);
  if (!executor->CheckAction(CAddonMediaImporterExecutor::Action::UpdateOnSource) ||
      executor->GetTask() == nullptr)
  {
    s_logger->error("cannot update imported item for handle {}", handle);
    return nullptr;
  }

  auto updateTask = dynamic_cast<CMediaImportUpdateTask*>(executor->GetTask());
  if (updateTask == nullptr)
  {
    s_logger->error("invalid import task ({}) to update imported item for handle {}",
                    static_cast<int>(executor->GetTask()->GetType()), handle);
    return nullptr;
  }

  return std::make_shared<CFileItem>(updateTask->GetItem());
}

void CAddonMediaImporter::FinishUpdateOnProvider(HandleType handle) throw(
    InvalidAddonMediaImporterHandleException)
{
  SetSuccess(handle, true, CAddonMediaImporterExecutor::Action::UpdateOnSource);
}

void CAddonMediaImporter::SetSuccess(
    HandleType handle,
    bool success,
    CAddonMediaImporterExecutor::Action action) throw(InvalidAddonMediaImporterHandleException)
{
  CAddonMediaImporterExecutor::GetExecutorFromHandle(handle)->SetSuccess(success, action);
}

CAddonMediaImporterObserver::CAddonMediaImporterObserver(const std::string& addonId)
  : CAddonMediaImporterBaseInternal(addonId, "CAddonMediaImporterObserver")
{
}

CAddonMediaImporterObserver::~CAddonMediaImporterObserver()
{
  Stop();
}

void CAddonMediaImporterObserver::Start()
{
  auto mediaImporter = GetAddon();
  if (mediaImporter == nullptr)
  {
    m_logger->warn("cannot start due to missing add-on");
    return;
  }

  // check if the media importer add-on has a observer service
  if (!mediaImporter->HasObserverService())
    return;

  // try to start the observer service
  if (!CServiceBroker::GetMediaImportAddons().StartObserver(mediaImporter))
  {
    m_logger->warn("failed to start");
    return;
  }

  m_started = true;
  m_logger->debug("successfully started");
}

void CAddonMediaImporterObserver::OnSourceAdded(const CMediaImportSource& source)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnSourceAdded(m_addonId, source);
#endif
}

void CAddonMediaImporterObserver::OnSourceUpdated(const CMediaImportSource& source)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnSourceUpdated(m_addonId, source);
#endif
}

void CAddonMediaImporterObserver::OnSourceRemoved(const CMediaImportSource& source)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnSourceRemoved(m_addonId, source);
#endif
}

void CAddonMediaImporterObserver::OnSourceActivated(const CMediaImportSource& source)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnSourceActivated(m_addonId, source);
#endif
}

void CAddonMediaImporterObserver::OnSourceDeactivated(const CMediaImportSource& source)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnSourceDeactivated(m_addonId, source);
#endif
}

void CAddonMediaImporterObserver::OnImportAdded(const CMediaImport& import)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnImportAdded(m_addonId, import);
#endif
}

void CAddonMediaImporterObserver::OnImportUpdated(const CMediaImport& import)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnImportUpdated(m_addonId, import);
#endif
}

void CAddonMediaImporterObserver::OnImportRemoved(const CMediaImport& import)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnImportRemoved(m_addonId, import);
#endif
}

void CAddonMediaImporterObserver::Stop()
{
  if (!m_started)
    return;

  auto mediaImporter = GetAddon();
  if (mediaImporter == nullptr)
  {
    m_logger->warn("cannot stop due to missing add-on");
    return;
  }

  // check if the media importer add-on has an observer service
  if (!mediaImporter->HasObserverService())
    return;

  // try to stop the observer service
  if (!CServiceBroker::GetMediaImportAddons().StopObserver(mediaImporter))
  {
    m_logger->warn("failed to stop");
    return;
  }

  m_logger->debug("successfully stopped");
}

CAddonMediaImporterFactory::CAddonMediaImporterFactory(const std::string& addonId)
  : CAddonMediaImporterBaseInternal(addonId, "CAddonMediaImporterFactory")
{
}

std::unique_ptr<IMediaImporterDiscoverer> CAddonMediaImporterFactory::CreateDiscoverer() const
{
  return std::make_unique<CAddonMediaImporterDiscoverer>(m_addonId);
}

std::unique_ptr<IMediaImporter> CAddonMediaImporterFactory::CreateImporter() const
{
  return std::make_unique<CAddonMediaImporter>(m_addonId);
}

std::unique_ptr<IMediaImporterObserver> CAddonMediaImporterFactory::CreateObserver() const
{
  return std::make_unique<CAddonMediaImporterObserver>(m_addonId);
}
