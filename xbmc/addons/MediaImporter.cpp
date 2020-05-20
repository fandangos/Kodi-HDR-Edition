/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImporter.h"

#include "AddonManager.h"
#include "ServiceBroker.h"
#include "interfaces/generic/ScriptInvocationManager.h"
#include "media/import/MediaImportManager.h"
#include "media/import/importers/AddonMediaImporter.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

namespace ADDON
{

CMediaImporter::CMediaImporter(const AddonInfoPtr& addonInfo)
  : CAddon(addonInfo, ADDON_MEDIAIMPORTER)
{
  const auto& addonTypeInfo = addonInfo->Type(ADDON_MEDIAIMPORTER);
  m_discoveryService = addonTypeInfo->GetValue("@discovery").asString();
  m_observerService = addonTypeInfo->GetValue("@observer").asString();

  m_canLookupProvider = addonTypeInfo->GetValue("canlookupprovider").asBoolean();
  m_providerLookupProtocol = addonTypeInfo->GetValue("canlookupprovider@protocol").asString();

  const auto automaticallyAddAsProvider = addonTypeInfo->GetElement("automaticallyaddasprovider");
  if (automaticallyAddAsProvider != nullptr)
    SetSupportedMediaTypes(automaticallyAddAsProvider->GetValue("supportedmediatypes").asString());

  ParseSettingsElement(addonTypeInfo, "providersettings", m_prepareProviderSettings,
                       m_providerSettingsPath);
  ParseSettingsElement(addonTypeInfo, "importsettings", m_prepareImportSettings,
                       m_importSettingsPath);
}

std::string CMediaImporter::ProviderLookupProtocol() const
{
  if (!m_providerLookupProtocol.empty())
    return m_providerLookupProtocol;

  return Name();
}

std::string CMediaImporter::DiscoveryServicePath() const
{
  if (!HasDiscoveryService())
    return "";

  return URIUtils::AddFileToFolder(m_addonInfo->Path(), m_discoveryService);
}

std::string CMediaImporter::ObserverServicePath() const
{
  if (!HasObserverService())
    return "";

  return URIUtils::AddFileToFolder(m_addonInfo->Path(), m_observerService);
}

bool CMediaImporter::AutomaticallyAddAsProvider() const
{
  return !m_supportedMediaTypes.empty();
}

bool CMediaImporter::PrepareProviderSettings() const
{
  return m_prepareProviderSettings && !m_providerSettingsPath.empty();
}

std::string CMediaImporter::ProviderSettingsPath() const
{
  if (m_providerSettingsPath.empty())
    return "";

  return URIUtils::AddFileToFolder(m_addonInfo->Path(), "resources", m_providerSettingsPath);
}

bool CMediaImporter::PrepareImportSettings() const
{
  return m_prepareImportSettings && !m_importSettingsPath.empty();
}

std::string CMediaImporter::ImportSettingsPath() const
{
  if (m_importSettingsPath.empty())
    return "";

  return URIUtils::AddFileToFolder(m_addonInfo->Path(), "resources", m_importSettingsPath);
}

void CMediaImporter::SetSupportedMediaTypes(const std::string& supportedMediaTypes)
{
  const auto mediaTypes = StringUtils::Split(supportedMediaTypes, " ");
  for (auto strMediaType : mediaTypes)
  {
    StringUtils::Trim(strMediaType);
    if (strMediaType.empty())
      continue;

    auto mediaType = CMediaTypes::FromString(strMediaType);
    if (mediaType != MediaTypeNone)
      m_supportedMediaTypes.insert(std::move(mediaType));
  }
}

void CMediaImporter::ParseSettingsElement(const CAddonExtensions* parent,
                                          const std::string& id,
                                          bool& prepareSettings,
                                          std::string& settingsPath)
{
  prepareSettings = false;
  if (parent == nullptr)
    return;

  prepareSettings = true;
  settingsPath = parent->GetValue(id).asString();
  if (!settingsPath.empty())
  {
    const auto prepareSettingsValue = parent->GetValue(id + "@prepare");
    if (!prepareSettingsValue.empty())
      prepareSettings = prepareSettingsValue.asBoolean();
  }
}

CMediaImportAddonManager::CMediaImportAddonManager(CAddonMgr& addonMgr)
  : m_addonMgr(addonMgr),
    m_logger(CServiceBroker::GetLogging().GetLogger("CMediaImportAddonManager"))
{
}

CMediaImportAddonManager::~CMediaImportAddonManager()
{
  Stop();
}

void CMediaImportAddonManager::Start()
{
  if (m_started)
    return;

  m_addonMgr.Events().Subscribe(this, &CMediaImportAddonManager::OnEvent);
  m_started = true;

  VECADDONS addons;
  if (m_addonMgr.GetAddons(addons, ADDON_MEDIAIMPORTER))
  {
    for (const auto& addon : addons)
      Register(addon);
  }
}

bool CMediaImportAddonManager::StartDiscovery(const std::string& addonId)
{
  if (!m_started)
    return false;

  const auto addon = GetAddon(addonId);
  return StartDiscovery(addon);
}

bool CMediaImportAddonManager::StartDiscovery(const AddonPtr& addon)
{
  if (!m_started || addon == nullptr)
    return false;

  CSingleLock lock(m_criticalSection);
  auto service = m_services.find(addon->ID());
  if (service == m_services.end())
    service = m_services.emplace(addon->ID(), ServiceHandlers{}).first;
  else if (service->second.discovery >= 0)
  {
    m_logger->debug("discovery service for {:s} already started.", addon->ID());
    return true;
  }

  auto mediaImporter = std::dynamic_pointer_cast<CMediaImporter>(addon);
  if (mediaImporter == nullptr || !mediaImporter->HasDiscoveryService())
    return false;

  if (!StringUtils::EndsWith(mediaImporter->DiscoveryServicePath(), ".py"))
    return false;

  m_logger->debug("starting discovery service for {:s}", addon->ID());
  auto handle = CScriptInvocationManager::GetInstance().ExecuteAsync(
      mediaImporter->DiscoveryServicePath(), addon);
  if (handle < 0)
  {
    m_logger->debug("discovery service for {:s} failed to start", addon->ID());
    return false;
  }
  service->second.discovery = handle;

  return true;
}

bool CMediaImportAddonManager::StartObserver(const std::string& addonId)
{
  if (!m_started)
    return false;

  const auto addon = GetAddon(addonId);
  return StartObserver(addon);
}

bool CMediaImportAddonManager::StartObserver(const AddonPtr& addon)
{
  if (!m_started || addon == nullptr)
    return false;

  CSingleLock lock(m_criticalSection);
  auto service = m_services.find(addon->ID());
  if (service == m_services.end())
    service = m_services.emplace(addon->ID(), ServiceHandlers{}).first;
  else if (service->second.observer >= 0)
  {
    m_logger->debug("observer service for {:s} already started.", addon->ID());
    return true;
  }

  auto mediaImporter = std::dynamic_pointer_cast<CMediaImporter>(addon);
  if (mediaImporter == nullptr || !mediaImporter->HasObserverService())
    return false;

  if (!StringUtils::EndsWith(mediaImporter->ObserverServicePath(), ".py"))
    return false;

  m_logger->debug("starting observer service for {:s}", addon->ID());
  auto handle = CScriptInvocationManager::GetInstance().ExecuteAsync(
      mediaImporter->ObserverServicePath(), addon);
  if (handle < 0)
  {
    m_logger->error("observer service for {:s} failed to start", addon->ID());
    return false;
  }
  service->second.observer = handle;

  return true;
}

void CMediaImportAddonManager::Stop()
{
  if (!m_started)
    return;

  m_addonMgr.Events().Unsubscribe(this);

  VECADDONS addons;
  if (m_addonMgr.GetAddons(addons, ADDON_MEDIAIMPORTER))
  {
    for (const auto& addon : addons)
      Unregister(addon);
  }

  m_started = false;
}

bool CMediaImportAddonManager::StopDiscovery(const std::string& addonId)
{
  if (!m_started)
    return false;

  const auto addon = GetAddon(addonId);
  return StopDiscovery(addon);
}

bool CMediaImportAddonManager::StopDiscovery(const AddonPtr& addon)
{
  if (!m_started || addon == nullptr)
    return false;

  CSingleLock lock(m_criticalSection);
  auto it = m_services.find(addon->ID());
  if (it == m_services.end())
    return false;

  const auto result = Stop(addon, it->second.discovery);
  Cleanup(it);
  return result;
}

bool CMediaImportAddonManager::StopObserver(const std::string& addonId)
{
  if (!m_started)
    return false;

  const auto addon = GetAddon(addonId);
  return StopObserver(addon);
}

bool CMediaImportAddonManager::StopObserver(const AddonPtr& addon)
{
  if (!m_started || addon == nullptr)
    return false;

  CSingleLock lock(m_criticalSection);
  auto it = m_services.find(addon->ID());
  if (it == m_services.end())
    return false;

  const auto result = Stop(addon, it->second.observer);
  Cleanup(it);
  return result;
}

AddonPtr CMediaImportAddonManager::GetAddon(const std::string& addonId) const
{
  AddonPtr addon;
  if (!m_addonMgr.GetAddon(addonId, addon, ADDON_MEDIAIMPORTER))
    return nullptr;

  return addon;
}

void CMediaImportAddonManager::OnEvent(const AddonEvent& event)
{
  if (typeid(event) == typeid(AddonEvents::Enabled))
    Register(event.id);
  else if (typeid(event) == typeid(AddonEvents::ReInstalled))
  {
    Unregister(event.id);
    Register(event.id);
  }
  else if (typeid(event) == typeid(AddonEvents::Disabled) ||
           typeid(event) == typeid(AddonEvents::UnInstalled))
    Unregister(event.id);
}

void CMediaImportAddonManager::Register(const std::string& addonId)
{
  const auto addon = GetAddon(addonId);
  Register(addon);
}

void CMediaImportAddonManager::Register(const AddonPtr& addon)
{
  if (addon == nullptr)
    return;

  // register the importer
  CServiceBroker::GetMediaImportManager().RegisterImporter(
      std::make_shared<CAddonMediaImporterFactory>(addon->ID()));
}

void CMediaImportAddonManager::Unregister(const std::string& addonId)
{
  const auto addon = GetAddon(addonId);
  Unregister(addon);
}

void CMediaImportAddonManager::Unregister(const AddonPtr& addon)
{
  if (addon == nullptr)
    return;

  // unregister the importer
  CServiceBroker::GetMediaImportManager().UnregisterImporter(addon->ID());

  // make sure that all services are stopped
  Stop(addon);
}

bool CMediaImportAddonManager::Stop(const std::string& addonId)
{
  const auto addon = GetAddon(addonId);
  return Stop(addon);
}

bool CMediaImportAddonManager::Stop(const AddonPtr& addon)
{
  if (!m_started || addon == nullptr)
    return false;

  auto result = StopDiscovery(addon);
  result &= StopObserver(addon);
  return result;
}

bool CMediaImportAddonManager::Stop(const AddonPtr& addon, int& handle)
{
  m_logger->debug("stopping {:s}", addon->ID());
  if (!CScriptInvocationManager::GetInstance().Stop(handle))
  {
    CLog::Log(LOGINFO, "CMediaImportAddonManager: failed to stop {:s} (may have ended)",
              addon->ID());
    return false;
  }

  handle = -1;

  return true;
}

void CMediaImportAddonManager::Cleanup(ServicesMap::iterator service)
{
  if (service->second.discovery >= 0 || service->second.observer >= 0)
    return;

  m_services.erase(service);
}

} /*namespace ADDON*/
