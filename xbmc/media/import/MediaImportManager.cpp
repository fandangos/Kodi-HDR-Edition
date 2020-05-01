/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportManager.h"

#include "FileItem.h"
#include "GUIUserMessages.h"
#include "LibraryQueue.h"
#include "ServiceBroker.h"
#include "dialogs/GUIDialogProgress.h"
#include "events/EventLog.h"
#include "events/MediaImportEvent.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "interfaces/AnnouncementManager.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/IMediaImportRepository.h"
#include "media/import/IMediaImporter.h"
#include "media/import/MediaImport.h"
#include "media/import/MediaImportSource.h"
#include "media/import/handlers/EpisodeImportHandler.h"
#include "media/import/handlers/MovieImportHandler.h"
#include "media/import/handlers/MovieSetImportHandler.h"
#include "media/import/handlers/MusicVideoImportHandler.h"
#include "media/import/handlers/SeasonImportHandler.h"
#include "media/import/handlers/TvShowImportHandler.h"
#include "media/import/jobs/MediaImportSourceActivationJob.h"
#include "media/import/jobs/MediaImportSourceJobBase.h"
#include "media/import/jobs/MediaImportSourceReadyJob.h"
#include "media/import/jobs/MediaImportSourceRegistrationJob.h"
#include "media/import/jobs/MediaImportTaskProcessorJob.h"
#include "media/import/jobs/tasks/MediaImportChangesetTask.h"
#include "media/import/jobs/tasks/MediaImportImportItemsRetrievalTask.h"
#include "media/import/jobs/tasks/MediaImportLocalItemsRetrievalTask.h"
#include "media/import/jobs/tasks/MediaImportRemovalTask.h"
#include "media/import/jobs/tasks/MediaImportSynchronisationTask.h"
#include "media/import/jobs/tasks/MediaImportUpdateTask.h"
#include "threads/SingleLock.h"
#include "utils/SpecialSort.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"

#include <algorithm>
#include <string.h>
#include <tuple>

#include <fmt/ostream.h>

CMediaImportManager::CMediaImportManager()
  : CJobQueue(false, 1, CJob::PRIORITY_NORMAL),
    m_logger(CServiceBroker::GetLogging().GetLogger("CMediaImportManager")),
    m_initialized(false),
    m_manuallyAddedSourceTimer(this)
{
}

CMediaImportManager::~CMediaImportManager()
{
  Uninitialize();
}

void CMediaImportManager::Initialize()
{
  if (m_initialized)
    return;

  // register media handlers
  RegisterMediaImportHandler(std::make_shared<CMovieImportHandler>(this));
  RegisterMediaImportHandler(std::make_shared<CMovieSetImportHandler>(this));
  RegisterMediaImportHandler(std::make_shared<CTvShowImportHandler>(this));
  RegisterMediaImportHandler(std::make_shared<CSeasonImportHandler>(this));
  RegisterMediaImportHandler(std::make_shared<CEpisodeImportHandler>(this));
  RegisterMediaImportHandler(std::make_shared<CMusicVideoImportHandler>(this));

  // start all registered importers
  {
    CSingleLock importersLock(m_importersLock);
    for (auto&& importer : m_importers)
    {
      importer.second.discoverer = importer.second.factory->CreateDiscoverer();
      importer.second.discoverer->Start();

      importer.second.observer = importer.second.factory->CreateObserver();
      importer.second.observer->Start();
    }
  }

  // start the timer responsible for checking whether manually imported sources are active or not
  m_manuallyAddedSourceTimer.Start(1000, true);

  m_initialized = true;
}

void CMediaImportManager::Uninitialize()
{
  // stop the timer responsible for checking whether manually imported sources are active or not
  m_manuallyAddedSourceTimer.Stop();

  // cancel all MediaImportTaskProcessorJobs
  CancelAllJobs();

  // clear all importers
  {
    CSingleLock lock(m_importersLock);
    m_importers.clear();
  }

  // clear all media handlers
  {
    CSingleLock lock(m_importHandlersLock);
    m_importHandlers.clear();
    m_importHandlersMap.clear();
  }

  // deactivate all sources
  {
    CSingleLock lock(m_sourcesLock);
    for (const auto& it : m_sources)
      DeactivateSource(it.first);
    m_sources.clear();
  }

  // clear all import repositories
  {
    CSingleLock lock(m_importRepositoriesLock);
    m_importRepositories.clear();
  }
}

void CMediaImportManager::RegisterImportRepository(MediaImportRepositoryPtr importRepository)
{
  if (importRepository == nullptr || !importRepository->Initialize())
  {
    m_logger->warn("failed to register and initialize given import repository");
    return;
  }

  std::vector<CMediaImportSource> sources = importRepository->GetSources();

  CSingleLock repositoriesLock(m_importRepositoriesLock);
  m_importRepositories.insert(importRepository);

  // add the sources from the repository. if the same source is in multiple
  // repositories, the std::map::insert will not overwrite the existing entry
  CSingleLock sourcesLock(m_sourcesLock);
  for (const auto& itSource : sources)
  {
    MediaImportSource importSource = {itSource.GetImporterId(), false, false, false};
    m_sources.insert(std::make_pair(itSource.GetIdentifier(), importSource));
  }

  m_logger->debug("new import repository with {} sources added", sources.size());
}

bool CMediaImportManager::UnregisterImportRepository(
    const MediaImportRepositoryPtr importRepository)
{
  if (importRepository == nullptr)
    return false;

  CSingleLock repositoriesLock(m_importRepositoriesLock);
  const auto& it = m_importRepositories.find(importRepository);
  if (it == m_importRepositories.end())
    return false;

  // remove all sources from that repository
  std::vector<CMediaImportSource> sources = importRepository->GetSources();
  {
    CSingleLock sourceslock(m_sourcesLock);
    for (const auto& itSource : sources)
    {
      // only remove the source if it isn't part of another repository as well
      CMediaImportSource source(itSource.GetIdentifier());
      if (!FindSource(source.GetIdentifier(), source))
        m_sources.erase(itSource.GetIdentifier());
    }
  }

  m_importRepositories.erase(it);

  m_logger->debug("import repository with {} sources removed", sources.size());
  return true;
}

void CMediaImportManager::RegisterImporter(MediaImporterFactoryPtr importer)
{
  if (importer == nullptr)
    return;

  {
    CSingleLock importersLock(m_importersLock);
    if (m_importers.find(importer->GetIdentification()) != m_importers.end())
      return;
  }

  MediaImporter internalImporter;
  internalImporter.factory = importer;

  {
    CSingleLock importersLock(m_importersLock);
    const auto inserted = m_importers.emplace(std::string(importer->GetIdentification()),
                                              std::move(internalImporter));
    if (inserted.second)
    {
      // if the importer has been added after Initialize() has been called, start the importer automatically
      if (m_initialized)
      {
        inserted.first->second.discoverer = importer->CreateDiscoverer();
        inserted.first->second.discoverer->Start();

        inserted.first->second.observer = importer->CreateObserver();
        inserted.first->second.observer->Start();
      }

      m_logger->debug("new importer {} added", importer->GetIdentification());
    }
  }
}

bool CMediaImportManager::UnregisterImporter(const std::string& importerIdentifier)
{
  CSingleLock lock(m_importersLock);
  auto&& it = m_importers.find(importerIdentifier);
  if (it == m_importers.end())
    return false;

  m_logger->debug("importer {} removed", it->second.factory->GetIdentification());
  m_importers.erase(it);
  lock.Leave();

  return true;
}

std::vector<MediaImporterFactoryConstPtr> CMediaImportManager::GetImporters() const
{
  std::vector<MediaImporterFactoryConstPtr> importer;

  CSingleLock lock(m_importersLock);
  for (const auto& it : m_importers)
    importer.push_back(std::const_pointer_cast<const IMediaImporterFactory>(it.second.factory));

  return importer;
}

bool CMediaImportManager::DiscoverSource(const std::string& importerIdentification,
                                         CMediaImportSource& source)
{
  if (importerIdentification.empty())
    return false;

  auto importer = GetImporterById(importerIdentification);
  if (importer == nullptr)
    return false;

  if (!importer->CanLookupSource())
    return false;

  if (!importer->DiscoverSource(source))
    return false;

  source.SetImporterId(importerIdentification);
  return true;
}

bool CMediaImportManager::HasImporter(const std::string& id) const
{
  CSingleLock lock(m_importersLock);
  return m_importers.find(id) != m_importers.end();
}

MediaImporterPtr CMediaImportManager::GetImporterById(const std::string& id) const
{
  if (id.empty())
    return nullptr;

  CSingleLock lock(m_importersLock);
  const auto& importer = m_importers.find(id);
  if (importer == m_importers.end())
    return nullptr;

  return importer->second.factory->CreateImporter();
}

MediaImporterPtr CMediaImportManager::GetImporterBySource(const CMediaImportSource& source) const
{
  std::string importerId = source.GetImporterId();
  if (importerId.empty())
  {
    CSingleLock sourcesLock(m_sourcesLock);
    const auto& itSource = m_sources.find(source.GetIdentifier());
    if (itSource == m_sources.end())
      return nullptr;

    importerId = itSource->second.importerId;
  }

  return GetImporterById(importerId);
}

void CMediaImportManager::RegisterMediaImportHandler(MediaImportHandlerPtr importHandler)
{
  if (importHandler == nullptr)
    return;

  CSingleLock lock(m_importHandlersLock);
  if (m_importHandlersMap.find(importHandler->GetMediaType()) == m_importHandlersMap.end())
  {
    m_importHandlersMap.insert(make_pair(importHandler->GetMediaType(), importHandler));

    // build a dependency list
    std::vector<std::pair<MediaType, MediaType>> dependencies;
    for (const auto& itHandler : m_importHandlersMap)
    {
      GroupedMediaTypes mediaTypes = itHandler.second->GetGroupedMediaTypes();
      for (const auto& itMediaType : mediaTypes)
      {
        if (itMediaType == itHandler.first)
          break;

        dependencies.push_back(make_pair(itHandler.first, itMediaType));
      }
    }

    // re-sort the import handlers and their dependencies
    GroupedMediaTypes result = SpecialSort::SortTopologically(dependencies);
    std::map<MediaType, MediaImportHandlerConstPtr> handlersCopy(m_importHandlersMap.begin(),
                                                                 m_importHandlersMap.end());
    m_importHandlers.clear();
    for (const auto& it : result)
    {
      m_importHandlers.push_back(handlersCopy.find(it)->second);
      handlersCopy.erase(it);
    }
    for (const auto& itHandler : handlersCopy)
      m_importHandlers.push_back(itHandler.second);

    m_logger->debug("new import handler for {} added", importHandler->GetMediaType());
  }
}

void CMediaImportManager::UnregisterMediaImportHandler(MediaImportHandlerPtr importHandler)
{
  if (importHandler == nullptr)
    return;

  CSingleLock lock(m_importHandlersLock);
  auto&& it = m_importHandlersMap.find(importHandler->GetMediaType());
  if (it == m_importHandlersMap.end() || it->second != importHandler)
    return;

  // remove the import handler from the map
  m_importHandlersMap.erase(it);
  // and from the sorted vector
  m_importHandlers.erase(
      std::remove_if(m_importHandlers.begin(), m_importHandlers.end(),
                     [this, &importHandler](const MediaImportHandlerConstPtr& vecIt) {
                       if (vecIt == importHandler)
                       {
                         m_logger->debug("import handler for {} removed",
                                         importHandler->GetMediaType());
                         return true;
                       }

                       return false;
                     }),
      m_importHandlers.end());
}

MediaImportHandlerConstPtr CMediaImportManager::GetImportHandler(const MediaType& mediaType) const
{
  const auto& itMediaImportHandler = m_importHandlersMap.find(mediaType);
  if (itMediaImportHandler == m_importHandlersMap.end())
    return MediaImportHandlerPtr();

  return itMediaImportHandler->second;
}

std::vector<MediaImportHandlerConstPtr> CMediaImportManager::GetImportHandlers() const
{
  return m_importHandlers;
}

std::vector<MediaImportHandlerConstPtr> CMediaImportManager::GetImportHandlers(
    const MediaTypes& mediaTypes) const
{
  if (mediaTypes.empty())
    return GetImportHandlers();

  std::vector<MediaImportHandlerConstPtr> importHandlers;
  CSingleLock lock(m_importHandlersLock);
  std::copy_if(m_importHandlers.begin(), m_importHandlers.end(), importHandlers.begin(),
               [&mediaTypes](const MediaImportHandlerConstPtr& importHandler) {
                 return mediaTypes.find(importHandler->GetMediaType()) != mediaTypes.end();
               });

  return importHandlers;
}

bool CMediaImportManager::IsMediaTypeSupported(const MediaType& mediaType) const
{
  CSingleLock lock(m_importHandlersLock);

  const auto it = std::find_if(m_importHandlers.cbegin(), m_importHandlers.cend(),
                               [&mediaType](const MediaImportHandlerConstPtr& importHandler) {
                                 return importHandler->GetMediaType() == mediaType;
                               });

  return it != m_importHandlers.cend();
}

MediaTypes CMediaImportManager::GetSupportedMediaTypes() const
{
  MediaTypes mediaTypes;

  CSingleLock lock(m_importHandlersLock);
  // get all media types for which there are handlers
  for (const auto& it : m_importHandlers)
    mediaTypes.insert(it->GetMediaType());

  return mediaTypes;
}

GroupedMediaTypes CMediaImportManager::GetSupportedMediaTypesOrdered(
    bool reversed /* = false */) const
{
  GroupedMediaTypes mediaTypesOrdered;

  CSingleLock lock(m_importHandlersLock);
  // get all media types for which there are handlers
  for (const auto& it : m_importHandlers)
    mediaTypesOrdered.push_back(it->GetMediaType());

  if (reversed)
    std::reverse(mediaTypesOrdered.begin(), mediaTypesOrdered.end());

  return mediaTypesOrdered;
}

GroupedMediaTypes CMediaImportManager::GetSupportedMediaTypesOrdered(
    const MediaTypes& mediaTypes, bool reversed /* = false */) const
{
  GroupedMediaTypes mediaTypesOrdered;
  if (mediaTypes.empty())
    return GetSupportedMediaTypesOrdered();

  CSingleLock lock(m_importHandlersLock);
  for (const auto& it : m_importHandlers)
  {
    if (mediaTypes.find(it->GetMediaType()) != mediaTypes.end())
      mediaTypesOrdered.push_back(it->GetMediaType());
  }

  if (reversed)
    std::reverse(mediaTypesOrdered.begin(), mediaTypesOrdered.end());

  return mediaTypesOrdered;
}

std::vector<GroupedMediaTypes> CMediaImportManager::GetSupportedMediaTypesGrouped(
    const MediaTypes& mediaTypes) const
{
  std::vector<GroupedMediaTypes> supportedMediaTypes;
  MediaTypes handledMediaTypes;

  CSingleLock lock(m_importHandlersLock);
  // get all media types for which there are handlers
  for (const auto& handler : m_importHandlers)
  {
    if (mediaTypes.find(handler->GetMediaType()) == mediaTypes.end())
      continue;

    // make sure all required media types are available
    MediaTypes requiredMediaTypes = handler->GetRequiredMediaTypes();
    if (std::any_of(requiredMediaTypes.begin(), requiredMediaTypes.end(),
                    [&mediaTypes](const MediaType& mediaType) {
                      return mediaTypes.find(mediaType) == mediaTypes.end();
                    }))
      continue;

    handledMediaTypes.insert(handler->GetMediaType());
  }

  for (const auto& handler : m_importHandlers)
  {
    const auto& itHandledMediaType = handledMediaTypes.find(handler->GetMediaType());
    if (itHandledMediaType == handledMediaTypes.end())
      continue;

    GroupedMediaTypes group;
    GroupedMediaTypes groupedMediaTypes = handler->GetGroupedMediaTypes();
    for (const auto& itGroupedMediaType : groupedMediaTypes)
    {
      const auto& itMediaType = handledMediaTypes.find(itGroupedMediaType);
      if (itMediaType != handledMediaTypes.end())
      {
        group.push_back(*itMediaType);
        handledMediaTypes.erase(itMediaType);
      }
    }

    if (group.empty())
    {
      group.push_back(*itHandledMediaType);
      handledMediaTypes.erase(itHandledMediaType);
    }

    supportedMediaTypes.push_back(group);
  }

  return supportedMediaTypes;
}

GroupedMediaTypes CMediaImportManager::GetGroupedMediaTypes(const MediaType& mediaType) const
{
  if (mediaType.empty())
    return {};

  CSingleLock lock(m_importHandlersLock);
  const auto& itHandler = m_importHandlersMap.find(mediaType);
  if (itHandler == m_importHandlersMap.end())
    return {};

  return itHandler->second->GetGroupedMediaTypes();
}

bool CMediaImportManager::AddSource(const std::string& importerId,
                                    const std::string& sourceID,
                                    const std::string& basePath,
                                    const std::string& friendlyName,
                                    const std::string& iconUrl /* = "" */,
                                    const MediaTypes& mediaTypes /* = MediaTypes() */)
{
  CMediaImportSource source(sourceID, basePath, friendlyName, iconUrl, mediaTypes);
  source.SetImporterId(importerId);
  return AddSource(source);
}

bool CMediaImportManager::AddSource(const CMediaImportSource& source)
{
  return AddSourceAsync(source, false, false);
}

bool CMediaImportManager::AddAndActivateSource(const std::string& importerId,
                                               const std::string& sourceID,
                                               const std::string& basePath,
                                               const std::string& friendlyName,
                                               const std::string& iconUrl /* = "" */,
                                               const MediaTypes& mediaTypes /* = MediaTypes() */)
{
  CMediaImportSource source(sourceID, basePath, friendlyName, iconUrl, mediaTypes);
  source.SetImporterId(importerId);
  return AddAndActivateSource(source);
}

bool CMediaImportManager::AddAndActivateSource(const CMediaImportSource& source)
{
  return AddSourceAsync(source, true, false);
}

bool CMediaImportManager::AddSourceManually(const std::string& importerId,
                                            const std::string& sourceID,
                                            const std::string& basePath,
                                            const std::string& friendlyName,
                                            const std::string& iconUrl /* = "" */,
                                            const MediaTypes& mediaTypes /* = MediaTypes() */)
{
  CMediaImportSource source(sourceID, basePath, friendlyName, iconUrl, mediaTypes);
  source.SetImporterId(importerId);
  return AddSourceManually(source);
}

bool CMediaImportManager::AddSourceManually(const CMediaImportSource& source)
{
  return AddSourceAsync(source, false, true);
}

bool CMediaImportManager::AddAndActivateSourceManually(
    const std::string& importerId,
    const std::string& sourceID,
    const std::string& basePath,
    const std::string& friendlyName,
    const std::string& iconUrl /* = "" */,
    const MediaTypes& mediaTypes /* = MediaTypes() */)
{
  CMediaImportSource source(sourceID, basePath, friendlyName, iconUrl, mediaTypes);
  source.SetImporterId(importerId);
  return AddAndActivateSourceManually(source);
}

bool CMediaImportManager::AddAndActivateSourceManually(const CMediaImportSource& source)
{
  return AddSourceAsync(source, true, true);
}

bool CMediaImportManager::ActivateSource(const std::string& importerId,
                                         const std::string& sourceID,
                                         const std::string& basePath /* = "" */,
                                         const std::string& friendlyName /* = "" */,
                                         const std::string& iconUrl /* = "" */)
{
  CMediaImportSource source(sourceID, basePath, friendlyName, iconUrl);
  source.SetImporterId(importerId);
  return ActivateSource(source);
}

bool CMediaImportManager::ActivateSource(const CMediaImportSource& source)
{
  return ActivateSourceAsync(source);
}

void CMediaImportManager::DeactivateSource(const std::string& sourceID)
{
  if (sourceID.empty())
    return;

  {
    CSingleLock sourcesLock(m_sourcesLock);
    auto&& itSource = m_sources.find(sourceID);
    if (itSource == m_sources.end() || itSource->second.removing)
      return;

    itSource->second.active = false;
    itSource->second.ready = false;
  }

  // if there are tasks for the source which haven't started yet or are still running try to cancel them
  CancelJobs(sourceID);

  // disable all items imported from the unregistered source
  std::vector<CMediaImport> imports = GetImportsBySource(sourceID);
  {
    CSingleLock handlersLock(m_importHandlersLock);
    for (const auto& import : imports)
    {
      for (const auto& mediaType : import.GetMediaTypes())
      {
        auto&& itHandler = m_importHandlersMap.find(mediaType);
        if (itHandler != m_importHandlersMap.end())
        {
          auto importHandler = std::unique_ptr<IMediaImportHandler>(itHandler->second->Create());
          importHandler->SetImportedItemsEnabled(import, false);
        }
      }
    }
  }

  CMediaImportSource source(sourceID);
  if (!FindSource(sourceID, source))
    return;

  m_logger->debug("source {} deactivated", source);
  OnSourceDeactivated(source);
}

bool CMediaImportManager::UpdateSource(const CMediaImportSource& source)
{
  if (source.GetIdentifier().empty())
    return false;

  bool updated = false;
  {
    CSingleLock sourcesLock(m_sourcesLock);
    auto&& itSource = m_sources.find(source.GetIdentifier());
    if (itSource == m_sources.end() || itSource->second.removing)
      return false;

    bool success = false;
    {
      CSingleLock repositoriesLock(m_importRepositoriesLock);
      // try to update the source in at least one of the repositories
      for (auto& repository : m_importRepositories)
      {
        bool tmpUpdated;
        if (repository->UpdateSource(source, tmpUpdated))
        {
          success = true;
          updated |= tmpUpdated;
        }
      }
    }

    if (!success)
      return false;
  }

  if (updated)
  {
    AddJob(source.GetIdentifier(), new CMediaImportSourceReadyJob(source, this));

    m_logger->info("source ready task for {} started", source);
  }

  return true;
}

void CMediaImportManager::RemoveSource(const std::string& sourceID)
{
  if (sourceID.empty())
    return;

  {
    CSingleLock sourcesLock(m_sourcesLock);
    auto&& itSource = m_sources.find(sourceID);
    if (itSource == m_sources.end() || itSource->second.removing)
      return;

    // mark the source as being removed
    itSource->second.removing = true;
  }

  CMediaImportSource source(sourceID);
  if (!FindSource(sourceID, source))
    return;

  // if there are tasks for the source which haven't started yet or are still running try to cancel them
  CancelJobs(sourceID);

  std::vector<CMediaImport> imports;
  {
    CSingleLock importRepositoriesLock(m_importRepositoriesLock);
    for (const auto& repository : m_importRepositories)
    {
      std::vector<CMediaImport> repoImports = repository->GetImportsBySource(sourceID);
      for (auto& import : repoImports)
        imports.push_back(import);
    }
  }

  CMediaImportTaskProcessorJob* processorJob =
      CMediaImportTaskProcessorJob::Remove(source, imports, this, this);
  AddJob(source.GetIdentifier(), processorJob);

  m_logger->info("source removal task for {} started", source);
}

bool CMediaImportManager::HasSources() const
{
  return !m_sources.empty();
}

bool CMediaImportManager::HasSources(bool active) const
{
  if (!HasSources())
    return false;

  CSingleLock sourcesLock(m_sourcesLock);
  return std::any_of(m_sources.begin(), m_sources.end(),
                     [active](const std::pair<std::string, MediaImportSource>& source) {
                       return source.second.active == active;
                     });
}

std::vector<CMediaImportSource> CMediaImportManager::GetSources() const
{
  std::vector<CMediaImportSource> sources;
  {
    CSingleLock lock(m_importRepositoriesLock);
    for (const auto& repository : m_importRepositories)
    {
      std::vector<CMediaImportSource> tmpSources = repository->GetSources();
      sources.insert(sources.end(), tmpSources.begin(), tmpSources.end());
    }
  }

  std::map<std::string, CMediaImportSource> mapSources;
  {
    CSingleLock lock(m_sourcesLock);
    for (auto& source : sources)
    {
      const auto& itSourceLocal = m_sources.find(source.GetIdentifier());
      if (itSourceLocal == m_sources.end() || itSourceLocal->second.removing)
        continue;

      auto&& itSourceMap = mapSources.find(source.GetIdentifier());
      if (itSourceMap == mapSources.end())
      {
        // add the source to the list
        source.SetActive(itSourceLocal->second.active);
        source.SetReady(itSourceLocal->second.ready);
        mapSources.insert(std::make_pair(source.GetIdentifier(), source));
      }
      else
      {
        // check if we need to update the last synced timestamp
        if (itSourceMap->second.GetLastSynced() < source.GetLastSynced())
          itSourceMap->second.SetLastSynced(source.GetLastSynced());

        // update the list of media types
        MediaTypes mediaTypes = itSourceMap->second.GetAvailableMediaTypes();
        mediaTypes.insert(source.GetAvailableMediaTypes().begin(),
                          source.GetAvailableMediaTypes().end());
        itSourceMap->second.SetAvailableMediaTypes(mediaTypes);
      }
    }
  }

  sources.clear();
  for (const auto& itSourceMap : mapSources)
    sources.push_back(itSourceMap.second);

  return sources;
}

std::vector<CMediaImportSource> CMediaImportManager::GetSources(bool active) const
{
  const auto matchesSourceActive = [active](const CMediaImportSource& source) {
    return source.IsActive() != active;
  };

  auto sources = GetSources();
  sources.erase(std::remove_if(sources.begin(), sources.end(), matchesSourceActive), sources.end());
  return sources;
}

bool CMediaImportManager::GetSource(const std::string& sourceID, CMediaImportSource& source) const
{
  if (sourceID.empty())
    return false;

  CSingleLock lock(m_sourcesLock);
  return GetSourceInternal(sourceID, source);
}

bool CMediaImportManager::IsSourceActive(const std::string& sourceID) const
{
  if (sourceID.empty())
    return false;

  CSingleLock lock(m_sourcesLock);
  const auto& itSource = m_sources.find(sourceID);
  if (itSource == m_sources.end() || itSource->second.removing)
    return false;

  return itSource->second.active;
}

bool CMediaImportManager::IsSourceActive(const CMediaImportSource& source) const
{
  return IsSourceActive(source.GetIdentifier());
}

bool CMediaImportManager::IsSourceReady(const std::string& sourceID) const
{
  if (sourceID.empty())
    return false;

  CSingleLock lock(m_sourcesLock);
  const auto& itSource = m_sources.find(sourceID);
  if (itSource == m_sources.end() || itSource->second.removing)
    return false;

  return itSource->second.ready;
}

bool CMediaImportManager::IsSourceReady(const CMediaImportSource& source) const
{
  return IsSourceReady(source.GetIdentifier());
}

bool CMediaImportManager::HasImports() const
{
  if (m_sources.empty())
    return false;

  CSingleLock importRepositoriesLock(m_importRepositoriesLock);
  return std::any_of(
      m_importRepositories.begin(), m_importRepositories.end(),
      [](const MediaImportRepositoryPtr& repository) { return !repository->GetImports().empty(); });
}

bool CMediaImportManager::HasImports(const std::string& sourceID) const
{
  if (sourceID.empty())
    return false;

  {
    CSingleLock lock(m_sourcesLock);
    const auto& itSource = m_sources.find(sourceID);
    if (itSource == m_sources.end() || itSource->second.removing)
      return false;
  }

  CSingleLock importRepositoriesLock(m_importRepositoriesLock);
  return std::any_of(m_importRepositories.begin(), m_importRepositories.end(),
                     [sourceID](const MediaImportRepositoryPtr& repository) {
                       return !repository->GetImportsBySource(sourceID).empty();
                     });
}

bool CMediaImportManager::HasImports(const CMediaImportSource& source) const
{
  return HasImports(source.GetIdentifier());
}

bool CMediaImportManager::AddSelectiveImport(const std::string& sourceID,
                                             const std::string& path,
                                             const GroupedMediaTypes& mediaTypes)
{
  if (sourceID.empty() || path.empty() || mediaTypes.empty())
  {
    m_logger->error("unable to add new selective import from source \"{}\" with "
                    "path \"{}\" and media type \"{}\"",
                    sourceID, path, CMediaTypes::Join(mediaTypes));
    return false;
  }

  return AddImport(sourceID, path, mediaTypes, false);
}

bool CMediaImportManager::AddRecursiveImport(const std::string& sourceID,
                                             const std::string& path,
                                             const GroupedMediaTypes& mediaTypes)
{
  if (sourceID.empty() || path.empty() || mediaTypes.empty())
  {
    m_logger->error("unable to add new recursive import from source \"{}\" with "
                    "path \"{}\" and media type \"{}\"",
                    sourceID, path, CMediaTypes::Join(mediaTypes));
    return false;
  }

  return AddImport(sourceID, path, mediaTypes, true);
}

bool CMediaImportManager::AddRecursiveImports(const std::string& sourceID,
                                              const std::string& path,
                                              const std::set<GroupedMediaTypes>& mediaTypes)
{
  if (sourceID.empty() || path.empty() || mediaTypes.empty())
  {
    m_logger->error("unable to add new recursive imports from source \"{}\" with "
                    "path \"{}\" and media types ({})",
                    sourceID, path, mediaTypes.size());
    return false;
  }

  // check if the given source exists
  std::vector<CMediaImport> addedImports;
  {
    CSingleLock sourcesLock(m_sourcesLock);

    CMediaImportSource source(sourceID);
    auto&& itSource = m_sources.find(sourceID);
    if (itSource == m_sources.end() || // source doesn't exist
        itSource->second.removing || !FindSource(sourceID, source))
    {
      m_logger->warn("trying to add new recursive imports from unknown source \"{}\"", sourceID);
      return false;
    }

    CSingleLock importHandlersLock(m_importHandlersLock);
    for (const auto& itMediaTypes : mediaTypes)
    {
      // check if the import already exists
      CMediaImport import;
      if (FindImport(path, itMediaTypes, import))
      {
        m_logger->error("unable to add already existing recursive import from "
                        "source \"{}\" with path \"{}\" and media type \"{}\"",
                        sourceID, path, CMediaTypes::Join(itMediaTypes));
        continue;
      }

      bool mediaTypesHandled = true;
      for (const auto& mediaType : itMediaTypes)
      {
        // check if there's an import handler that can handle the given media type
        if (m_importHandlersMap.find(mediaType) == m_importHandlersMap.end())
        {
          m_logger->error(
              "unable to add new recursive import from source \"{}\" "
              "with path \"{}\" and media type \"{}\" because there is no matching import "
              "handler available",
              sourceID, path, mediaType);
          mediaTypesHandled = false;
          break;
        }
      }

      if (!mediaTypesHandled)
        continue;

      CMediaImport newImport = CMediaImport::CreateRecursive(path, itMediaTypes, source);
      if (!AddImport(newImport))
      {
        m_logger->error("failed to add new recursive import for source \"{}\" with "
                        "path \"{}\" and media type \"{}\" to any import repository",
                        sourceID, path, CMediaTypes::Join(itMediaTypes));
        continue;
      }

      addedImports.push_back(newImport);
    }
  }

  for (const auto& import : addedImports)
    OnImportAdded(import);

  return true;
}

bool CMediaImportManager::UpdateImport(const CMediaImport& import)
{
  if (import.GetPath().empty() || import.GetMediaTypes().empty())
    return false;

  bool success = false;
  bool updated = false;
  {
    CSingleLock importRepositoriesLock(m_importRepositoriesLock);
    for (const auto& repository : m_importRepositories)
    {
      bool tmpUpdated;
      if (repository->UpdateImport(import, tmpUpdated))
      {
        success = true;
        updated |= tmpUpdated;
      }
    }
  }

  if (!success)
    return false;

  if (updated)
    OnImportUpdated(import);

  return true;
}

void CMediaImportManager::RemoveImport(const std::string& path, const GroupedMediaTypes& mediaTypes)
{
  if (path.empty() || mediaTypes.empty())
    return;

  CMediaImport import;
  if (!FindImport(path, mediaTypes, import))
    return;

  CMediaImportTaskProcessorJob* processorJob =
      CMediaImportTaskProcessorJob::Remove(import, this, this);
  AddJob(import.GetSource().GetIdentifier(), processorJob);

  m_logger->info("import removal task for {} started", import);
}

std::vector<CMediaImport> CMediaImportManager::GetImports() const
{
  std::vector<CMediaImport> imports;
  {
    CSingleLock importRepositoriesLock(m_importRepositoriesLock);
    for (const auto& repository : m_importRepositories)
    {
      std::vector<CMediaImport> repoImports = repository->GetImports();
      imports.insert(imports.end(), repoImports.begin(), repoImports.end());
    }
  }

  PrepareImports(imports);
  return imports;
}

std::vector<CMediaImport> CMediaImportManager::GetImportsByMediaType(
    const GroupedMediaTypes& mediaTypes) const
{
  std::vector<CMediaImport> imports;
  if (mediaTypes.empty())
    return imports;

  {
    CSingleLock importRepositoriesLock(m_importRepositoriesLock);
    for (const auto& repository : m_importRepositories)
    {
      std::vector<CMediaImport> repoImports = repository->GetImportsByMediaType(mediaTypes);
      imports.insert(imports.end(), repoImports.begin(), repoImports.end());
    }
  }

  PrepareImports(imports);
  return imports;
}

std::vector<CMediaImport> CMediaImportManager::GetImportsBySource(const std::string& sourceID) const
{
  std::vector<CMediaImport> imports;
  if (sourceID.empty())
    return imports;

  CSingleLock sourcesLock(m_sourcesLock);
  const auto& itSource = m_sources.find(sourceID);
  if (itSource == m_sources.end() || itSource->second.removing)
    return imports;

  {
    CSingleLock importRepositoriesLock(m_importRepositoriesLock);
    for (const auto& repository : m_importRepositories)
    {
      std::vector<CMediaImport> repoImports = repository->GetImportsBySource(sourceID);
      imports.insert(imports.end(), repoImports.begin(), repoImports.end());
    }
  }

  PrepareImports(imports);
  return imports;
}

std::vector<CMediaImport> CMediaImportManager::GetImportsByPath(
    const std::string& path, bool includeSubDirectories /* = false */) const
{
  std::vector<CMediaImport> imports;
  if (path.empty())
    return imports;

  {
    CSingleLock importRepositoriesLock(m_importRepositoriesLock);
    for (const auto& repository : m_importRepositories)
    {
      std::vector<CMediaImport> repoImports =
          repository->GetImportsByPath(path, includeSubDirectories);
      imports.insert(imports.end(), repoImports.begin(), repoImports.end());
    }
  }

  PrepareImports(imports);
  return imports;
}

bool CMediaImportManager::GetImport(const std::string& path,
                                    const GroupedMediaTypes& mediaTypes,
                                    CMediaImport& import) const
{
  if (path.empty())
    return false;

  return FindImport(path, mediaTypes, import);
}

bool CMediaImportManager::IsImportReady(const std::string& path,
                                        const GroupedMediaTypes& mediaTypes) const
{
  CMediaImport import(path);
  return GetImport(path, mediaTypes, import) && IsImportReady(import);
}

bool CMediaImportManager::IsImportReady(const CMediaImport& import) const
{
  // try to get an importer that can import the given path
  const auto& importer = GetImporterBySource(import.GetSource());
  if (importer == nullptr)
    return false;

  CMediaImport tmpImport(import);
  return importer->IsImportReady(tmpImport);
}

bool CMediaImportManager::IsImported(const std::string& path) const
{
  return !GetImportsByPath(path, false).empty();
}

bool CMediaImportManager::IsImportedInHierarchy(const std::string& path) const
{
  return !GetImportsByPath(path, true).empty();
}

bool CMediaImportManager::Import()
{
  bool result = false;
  CSingleLock sourcesLock(m_sourcesLock);
  for (const auto& source : m_sources)
  {
    if (!source.second.active || !source.second.ready || source.second.removing)
      continue;

    result |= Import(source.first);
  }

  return result;
}

bool CMediaImportManager::Import(const std::string& sourceID)
{
  if (sourceID.empty())
  {
    m_logger->warn("unable to import from an empty source identifier");
    return false;
  }

  CMediaImportSource source(sourceID);
  if (!FindSource(sourceID, source))
    return false;

  Import(source, false);
  return true;
}

bool CMediaImportManager::Import(const std::string& path, const GroupedMediaTypes& mediaTypes)
{
  if (path.empty() || mediaTypes.empty())
  {
    m_logger->warn("unable to import from invalid path \"{}\" or without media types ({})", path,
                   CMediaTypes::Join(mediaTypes));
    return false;
  }

  CMediaImport import;
  if (!FindImport(path, mediaTypes, import))
    return false;

  // check if the source exists and is active
  CSingleLock sourcesLock(m_sourcesLock);
  auto&& itSource = m_sources.find(import.GetSource().GetIdentifier());
  if (itSource == m_sources.end() || // source doesn't exist
      itSource->second.removing || !itSource->second.active)
  {
    m_logger->warn("unable to import from unregistered source \"{}\"", import.GetSource());
    return false;
  }

  // import media items from the given import (and all other imports whose media types depend upon this import's one or are grouped together with it)
  Import(import);

  return true;
}

void CMediaImportManager::Import(const CMediaImportSource& source, bool automatically /* = false */)
{
  auto& sourceInfo = m_sources[source.GetIdentifier()];
  if (!sourceInfo.active || !sourceInfo.ready)
    return;

  auto imports = GetImportsBySource(source.GetIdentifier());
  bool importStarted = false;
  for (const auto& import : imports)
  {
    auto processorJob =
        CMediaImportTaskProcessorJob::Import(import, automatically, this, this, this);
    if (processorJob == nullptr)
      continue;

    AddJob(source.GetIdentifier(), processorJob);
    importStarted = true;
  }

  if (importStarted)
    m_logger->info("import tasks for source {} started", source);
}

void CMediaImportManager::Import(const CMediaImport& import, bool automatically /* = false */)
{
  CMediaImportTaskProcessorJob* processorJob =
      CMediaImportTaskProcessorJob::Import(import, automatically, this, this, this);
  AddJob(import.GetSource().GetIdentifier(), processorJob);

  m_logger->info("import task for {} started", import);
}

CMediaImportManager::MediaImporterMap::iterator CMediaImportManager::GetImporterBySourceInternal(
    const std::string& sourceId)
{
  CSingleLock sourcesLock(m_sourcesLock);
  const auto& source = m_sources.find(sourceId);
  if (source == m_sources.end() || source->second.importerId.empty())
    return m_importers.end();

  return m_importers.find(source->second.importerId);
}

void CMediaImportManager::PrepareImports(std::vector<CMediaImport>& imports) const
{
  CSingleLock sourcesLock(m_sourcesLock);
  for (auto itImport = imports.begin(); itImport != imports.end();)
  {
    const auto& itSource = m_sources.find(itImport->GetSource().GetIdentifier());
    if (itSource == m_sources.end() || itSource->second.removing)
      itImport = imports.erase(itImport);
    else
    {
      itImport->SetActive(itSource->second.active);
      itImport->SetReady(itSource->second.ready);
      ++itImport;
    }
  }
}

bool CMediaImportManager::AddSourceAsync(CMediaImportSource source,
                                         bool activate,
                                         bool manuallyAdded)
{
  if (source.GetIdentifier().empty() || source.GetFriendlyName().empty())
  {
    m_logger->warn("unable to add source {}", source);
    return false;
  }

  source.SetManuallyAdded(manuallyAdded);
  if (source.GetImporterId().empty() || !HasImporter(source.GetImporterId()))
  {
    m_logger->warn("unable to add source {} with invalid importer \"{}\"", source,
                   source.GetImporterId());
    return false;
  }

  bool sourceFound = false;
  {
    CSingleLock sourcesLock(m_sourcesLock);
    CMediaImportSource currentSource;
    auto&& itSource = m_sources.find(source.GetIdentifier());
    if (itSource != m_sources.end() && FindSource(source.GetIdentifier(), currentSource))
      sourceFound = true;
  }

  if (sourceFound)
  {
    if (activate)
      return ActivateSourceAsync(source);

    return true;
  }

  AddJob(source.GetIdentifier(), new CMediaImportSourceRegistrationJob(source, activate, this));

  m_logger->info("source registration task for {} started", source);

  return true;
}

bool CMediaImportManager::AddSourceSync(const CMediaImportSource& source)
{
  const auto importerId = source.GetImporterId();
  if (importerId.empty() || !HasImporter(importerId))
    return false;

  bool success = false;
  bool added = false;
  CSingleLock repositoriesLock(m_importRepositoriesLock);
  // try to add the source to at least one of the repositories
  for (auto& repository : m_importRepositories)
  {
    bool tmpAdded;
    if (repository->AddSource(source, tmpAdded))
    {
      success = true;
      added |= tmpAdded;
    }
  }

  if (!success)
    return false;

  if (!added)
    return true;

  MediaImportSource importSource = {importerId, true, false, false};
  {
    CSingleLock sourcesLock(m_sourcesLock);
    m_sources.emplace(source.GetIdentifier(), importSource);
  }

  m_logger->debug("new source {} registered", source);

  return true;
}

bool CMediaImportManager::ActivateSourceAsync(const CMediaImportSource& source)
{
  if (source.GetIdentifier().empty())
  {
    m_logger->warn("unable to activate invalid source {}", source);
    return false;
  }

  const auto importerId = source.GetImporterId();
  if (importerId.empty() || !HasImporter(importerId))
  {
    m_logger->warn("unable to activate source {} with invalid importer \"{}\"", source, importerId);
    return false;
  }

  CMediaImportSource updatedSource;
  {
    CSingleLock sourcesLock(m_sourcesLock);
    auto&& itSource = m_sources.find(source.GetIdentifier());
    if (itSource == m_sources.end() || !FindSource(source.GetIdentifier(), updatedSource))
    {
      m_logger->warn("unable to activate unknown source {}", source);
      return false;
    }
  }

  // update any provided values from the existing source
  if (!source.GetBasePath().empty())
    updatedSource.SetBasePath(source.GetBasePath());
  if (!source.GetFriendlyName().empty())
    updatedSource.SetFriendlyName(source.GetFriendlyName());
  if (!source.GetIconUrl().empty())
    updatedSource.SetIconUrl(source.GetIconUrl());

  AddJob(source.GetIdentifier(), new CMediaImportSourceActivationJob(updatedSource, this));

  m_logger->info("source activation task for {} started", source);

  return true;
}

bool CMediaImportManager::ActivateSourceSync(const CMediaImportSource& source, bool ready)
{
  const auto importerId = source.GetImporterId();
  if (importerId.empty() || !HasImporter(importerId))
    return false;

  bool updated = false;
  {
    CSingleLock sourcesLock(m_sourcesLock);
    CMediaImportSource currentSource;
    auto&& itSource = m_sources.find(source.GetIdentifier());
    if (itSource == m_sources.end() || !FindSource(source.GetIdentifier(), currentSource))
      return false;

    {
      CSingleLock repositoriesLock(m_importRepositoriesLock);
      // try to update the source in at least one of the repositories
      for (auto& repository : m_importRepositories)
      {
        bool tmpUpdated;
        if (repository->UpdateSource(source, tmpUpdated))
        {
          updated |= tmpUpdated;
        }
      }
    }

    // update the source's active flag
    itSource->second.active = true;
    // update the source's ready flag
    itSource->second.ready = ready;
  }
  m_logger->debug("source {} activated", source);

  if (updated)
  {
    m_logger->debug("source {} updated", source);
    OnSourceUpdated(source);
  }

  OnSourceActivated(source);

  // start processing all imports of the source
  Import(source, true);

  return true;
}

bool CMediaImportManager::GetSourceInternal(const std::string& sourceID,
                                            CMediaImportSource& source) const
{
  const auto& itSource = m_sources.find(sourceID);
  if (itSource == m_sources.end() || itSource->second.removing)
    return false;

  if (!FindSource(sourceID, source))
    return false;

  source.SetActive(itSource->second.active);
  source.SetReady(itSource->second.ready);
  return true;
}

bool CMediaImportManager::FindSource(const std::string& sourceID, CMediaImportSource& source) const
{
  CSingleLock repositoriesLock(m_importRepositoriesLock);
  // try to find the source in at least one of the repositories
  return std::any_of(m_importRepositories.begin(), m_importRepositories.end(),
                     [&sourceID, &source](const MediaImportRepositoryPtr& repository) {
                       return repository->GetSource(sourceID, source);
                     });
}

void CMediaImportManager::UpdateManuallyAddedSources()
{
  // get a copy of all sources
  std::vector<std::tuple<std::string, bool>> sourceInfos;
  {
    CSingleLock lock(m_sourcesLock);
    for (const auto& it : m_sources)
    {
      if (!it.second.removing)
        sourceInfos.emplace_back(it.first, it.second.active);
    }
  }

  // check all manually added sources
  for (const auto& sourceInfo : sourceInfos)
  {
    const auto sourceId = std::get<0>(sourceInfo);
    CMediaImportSource source(sourceId);
    if (!GetSourceInternal(sourceId, source))
      continue;

    // ignore automatically added sources
    if (!source.IsManuallyAdded())
      continue;

    // find the matching importer
    bool active = false;
    if (LookupSource(source))
      active = true;

    const auto wasActive = std::get<1>(sourceInfo);
    // activate / deactivate the source if necessary
    if (active && !wasActive)
      ActivateSource(source);
    else if (!active && wasActive)
      DeactivateSource(source.GetIdentifier());
  }
}

bool CMediaImportManager::LookupSource(const CMediaImportSource& source)
{
  auto importer = GetImporterBySource(source);
  if (importer == nullptr)
    return false;

  if (!importer->CanLookupSource())
    return false;

  return importer->LookupSource(source);
}

bool CMediaImportManager::AddImport(const std::string& sourceID,
                                    const std::string& path,
                                    const GroupedMediaTypes& mediaTypes,
                                    bool recursive)
{
  // check if the import already exists
  CMediaImport import;
  if (FindImport(path, mediaTypes, import))
  {
    m_logger->error("unable to add already existing import from source \"{}\" with "
                    "path \"{}\" and media type \"{}\"",
                    sourceID, path, CMediaTypes::Join(mediaTypes));
    return false;
  }

  // check if the given source exists
  CMediaImportSource source(sourceID);
  {
    CSingleLock sourcesLock(m_sourcesLock);
    if (!GetSourceInternal(sourceID, source))
    {
      m_logger->warn("trying to add new import from unknown source \"{}\"", sourceID);
      return false;
    }
  }

  {
    CSingleLock importHandlersLock(m_importHandlersLock);
    for (const auto& mediaType : mediaTypes)
    {
      // check if there's an import handler that can handle the given media type
      if (m_importHandlersMap.find(mediaType) == m_importHandlersMap.end())
      {
        m_logger->error("unable to add new import from source \"{}\" with path \"{}\" and "
                        "media type \"{}\" because there is no matching import handler available",
                        sourceID, path, mediaType);
        return false;
      }
    }
  }

  CMediaImport newImport;
  if (recursive)
    newImport = CMediaImport::CreateRecursive(path, mediaTypes, source);
  else
    newImport = CMediaImport::CreateSelective(path, mediaTypes, source);

  if (!AddImport(newImport))
  {
    m_logger->error("failed to add new import for source \"{}\" with path \"{}\" "
                    "and media type \"{}\" to any import repository",
                    sourceID, path, CMediaTypes::Join(mediaTypes));
    return false;
  }

  OnImportAdded(newImport);

  return true;
}

bool CMediaImportManager::AddImport(const CMediaImport& import)
{
  bool success = false;
  bool added = false;
  CSingleLock repositoriesLock(m_importRepositoriesLock);
  // try to add the import to at least one of the repositories
  for (auto& repository : m_importRepositories)
  {
    bool tmpAdded;
    if (repository->AddImport(import, tmpAdded))
    {
      success = true;
      added |= tmpAdded;
    }
  }

  if (!success)
    return false;

  if (added)
    CServiceBroker::GetEventLog().Add(EventPtr(new CMediaImportEvent(import, 39567)));

  return true;
}

bool CMediaImportManager::FindImport(const std::string& path,
                                     const GroupedMediaTypes& mediaTypes,
                                     CMediaImport& import) const
{
  CSingleLock repositoriesLock(m_importRepositoriesLock);
  // try to find the import in at least one of the repositories
  return std::any_of(m_importRepositories.begin(), m_importRepositories.end(),
                     [&path, &mediaTypes, &import](const MediaImportRepositoryPtr& repository) {
                       return repository->GetImport(path, mediaTypes, import);
                     });
}

bool CMediaImportManager::AddImportedItems(const CMediaImport& import, const CFileItemList& items)
{
  // make sure the import is known
  CMediaImport tmpImport;
  if (!FindImport(import.GetPath(), import.GetMediaTypes(), tmpImport))
    return false;

  ChangesetItems changedItems;
  // make sure the items belong to the media import
  for (const auto& item : items)
  {
    // check the media type
    if (!IsMediaTypeSupported(item->GetMediaType()) ||
        !import.ContainsMediaType(item->GetMediaType()))
    {
      m_logger->warn("item {} with media type \"{}\" not supported by the given "
                     "media import",
                     item->GetPath(), item->GetMediaType());
      continue;
    }

    changedItems.push_back(std::make_pair(MediaImportChangesetType::Added, item));
  }

  if (changedItems.empty())
    return false;

  CMediaImportTaskProcessorJob* processorJob =
      CMediaImportTaskProcessorJob::ChangeImportedItems(import, changedItems, this, this);
  AddJob(import.GetSource().GetIdentifier(), processorJob);

  m_logger->info("add imported items task for {} started", import);

  return true;
}

bool CMediaImportManager::UpdateImportedItems(const CMediaImport& import,
                                              const CFileItemList& items)
{
  // make sure the import is known
  CMediaImport tmpImport;
  if (!FindImport(import.GetPath(), import.GetMediaTypes(), tmpImport))
    return false;

  ChangesetItems changedItems;
  // make sure the items belong to the media import
  for (const auto& item : items)
  {
    // check the media type
    if (!IsMediaTypeSupported(item->GetMediaType()) ||
        !import.ContainsMediaType(item->GetMediaType()))
    {
      m_logger->warn("item {} with media type \"{}\" not supported by the given "
                     "media import",
                     item->GetPath(), item->GetMediaType());
      continue;
    }

    changedItems.push_back(std::make_pair(MediaImportChangesetType::Changed, item));
  }

  if (changedItems.empty())
    return false;

  CMediaImportTaskProcessorJob* processorJob =
      CMediaImportTaskProcessorJob::ChangeImportedItems(import, changedItems, this, this);
  AddJob(import.GetSource().GetIdentifier(), processorJob);

  m_logger->info("update imported items task for {} started", import);

  return true;
}

bool CMediaImportManager::RemoveImportedItems(const CMediaImport& import,
                                              const CFileItemList& items)
{
  // make sure the import is known
  CMediaImport tmpImport;
  if (!FindImport(import.GetPath(), import.GetMediaTypes(), tmpImport))
    return false;

  ChangesetItems changedItems;
  // make sure the items belong to the media import
  for (const auto& item : items)
  {
    // check the media type
    if (!IsMediaTypeSupported(item->GetMediaType()) ||
        !import.ContainsMediaType(item->GetMediaType()))
    {
      m_logger->warn("item {} with media type \"{}\" not supported by the given "
                     "media import",
                     item->GetPath(), item->GetMediaType());
      continue;
    }

    changedItems.push_back(std::make_pair(MediaImportChangesetType::Removed, item));
  }

  if (changedItems.empty())
    return false;

  CMediaImportTaskProcessorJob* processorJob =
      CMediaImportTaskProcessorJob::ChangeImportedItems(import, changedItems, this, this);
  AddJob(import.GetSource().GetIdentifier(), processorJob);

  m_logger->info("remove imported items task from {} started", import);

  return true;
}

bool CMediaImportManager::ChangeImportedItems(const CMediaImport& import,
                                              const ChangesetItems& items)
{
  // make sure the import is known
  CMediaImport tmpImport;
  if (!FindImport(import.GetPath(), import.GetMediaTypes(), tmpImport))
    return false;

  ChangesetItems changedItems;
  // make sure the items belong to the media import
  for (const auto& item : items)
  {
    // check the media type
    if (!IsMediaTypeSupported(item.second->GetMediaType()) ||
        !import.ContainsMediaType(item.second->GetMediaType()))
    {
      m_logger->warn("item {} with media type \"{}\" not supported by the given "
                     "media import",
                     item.second->GetPath(), item.second->GetMediaType());
      continue;
    }

    changedItems.push_back(item);
  }

  if (changedItems.empty())
    return false;

  CMediaImportTaskProcessorJob* processorJob =
      CMediaImportTaskProcessorJob::ChangeImportedItems(import, changedItems, this, this);
  AddJob(import.GetSource().GetIdentifier(), processorJob);

  m_logger->info("change imported items task for {} started", import);

  return true;
}

bool CMediaImportManager::UpdateImportedItemOnSource(const CFileItem& item)
{
  if (!item.IsImported())
    return false;

  std::string sourceID = item.GetSource();
  std::string importPath = item.GetImportPath();

  if (!IsMediaTypeSupported(item.GetMediaType()))
  {
    m_logger->warn("unable to update {} due to unknown media type", item.GetPath());
    return false;
  }

  // get all media types belonging to the item's media type
  GroupedMediaTypes mediaTypes = GetGroupedMediaTypes(item.GetMediaType());
  if (mediaTypes.empty())
  {
    m_logger->warn("unable to update {} due to unsupported media type ({})", item.GetPath(),
                   item.GetMediaType());
    return false;
  }

  CMediaImport import;
  if (!FindImport(importPath, mediaTypes, import))
  {
    m_logger->warn("unable to update {} due to no import found for {} of {}", item.GetPath(),
                   importPath, CMediaTypes::Join(mediaTypes));
    return false;
  }

  if (!import.Settings()->UpdatePlaybackMetadataOnSource())
    return false;

  if (!m_sources[import.GetSource().GetIdentifier()].active)
  {
    m_logger->warn("unable to update item {} on inactive source {}", item.GetPath(),
                   import.GetSource().GetIdentifier());
    return false;
  }

  CMediaImportTaskProcessorJob* processorJob =
      CMediaImportTaskProcessorJob::UpdateImportedItemOnSource(import, item, this, this);
  AddJob(import.GetSource().GetIdentifier(), processorJob);

  m_logger->info("import update task for {} on {} started", item.GetPath(), import);

  return true;
}

std::vector<CFileItemPtr> CMediaImportManager::GetImportedItemsBySource(
    const CMediaImportSource& source) const
{
  std::vector<CFileItemPtr> items;
  const auto imports = GetImportsBySource(source.GetIdentifier());
  for (const auto& import : imports)
  {
    const auto importedItems = GetImportedItemsByImport(import);
    items.insert(items.end(), importedItems.begin(), importedItems.end());
  }

  return items;
}

std::vector<CFileItemPtr> CMediaImportManager::GetImportedItemsByImport(
    const CMediaImport& import) const
{
  // make sure the import is known
  CMediaImport tmpImport;
  if (!FindImport(import.GetPath(), import.GetMediaTypes(), tmpImport))
    return {};

  std::map<MediaType, MediaImportHandlerPtr> importHandlers;
  for (const auto& mediaType : import.GetMediaTypes())
  {
    const auto importHandler = GetImportHandler(mediaType);
    if (importHandler == nullptr)
    {
      m_logger->warn("no import handler for unknown media type \"{}\" for import {}", mediaType,
                     import);
      continue;
    }
    importHandlers.emplace(mediaType, importHandler->Create());
  }

  CMediaImportLocalItemsRetrievalTask localItemsRetrievalTask(tmpImport, importHandlers);
  if (!localItemsRetrievalTask.DoWork())
  {
    m_logger->warn("failed to retrieve imported items for import {}", import);
    return {};
  }

  std::vector<CFileItemPtr> items;
  for (const auto& mediaType : import.GetMediaTypes())
  {
    const auto& importedItems = localItemsRetrievalTask.GetLocalItems(mediaType);
    items.insert(items.end(), importedItems.begin(), importedItems.end());
  }
  return items;
}

void CMediaImportManager::AddJob(const std::string& sourceID, CMediaImportSourceJobBase* job)
{
  if (job == nullptr)
    return;

  CSingleLock jobsLock(m_sourceJobsLock);
  m_sourceJobMap[sourceID].insert(job);

  CJobQueue::AddJob(job);
}

void CMediaImportManager::AddJob(const std::string& sourceID, CMediaImportTaskProcessorJob* job)
{
  if (job == nullptr)
    return;

  CSingleLock jobsLock(m_importJobsLock);
  m_importJobMap[sourceID].insert(job);

  CLibraryQueue::GetInstance().AddJob(job, this);
}

template<class TJob>
void RemoveJob(const std::string& sourceID,
               const TJob* job,
               std::map<std::string, std::set<TJob*>>& jobMap,
               Logger logger)
{
  auto&& itJobs = jobMap.find(sourceID);
  if (itJobs != jobMap.end())
  {
    itJobs->second.erase(const_cast<TJob*>(job));
    if (itJobs->second.empty())
      jobMap.erase(itJobs);
  }
  else
    logger->warn("failed to remove task for source \"{}\"", sourceID);
}

void CMediaImportManager::RemoveJob(const std::string& sourceID,
                                    const CMediaImportSourceJobBase* job)
{
  CSingleLock jobsLock(m_sourceJobsLock);
  ::RemoveJob(sourceID, job, m_sourceJobMap, m_logger);
}

void CMediaImportManager::RemoveJob(const std::string& sourceID,
                                    const CMediaImportTaskProcessorJob* job)
{
  CSingleLock jobsLock(m_importJobsLock);
  ::RemoveJob(sourceID, job, m_importJobMap, m_logger);
}

void CMediaImportManager::CancelJobs(const std::string& sourceID)
{
  bool cancelled = false;
  {
    CSingleLock jobsLock(m_importJobsLock);
    const auto& itJobs = m_importJobMap.find(sourceID);
    if (itJobs != m_importJobMap.end())
    {
      for (const auto& itJob : itJobs->second)
      {
        if (itJob != nullptr)
          CLibraryQueue::GetInstance().CancelJob(itJob);
      }

      m_importJobMap.erase(sourceID);
      cancelled = true;
    }
  }

  {
    CSingleLock jobsLock(m_sourceJobsLock);
    const auto& itJobs = m_sourceJobMap.find(sourceID);
    if (itJobs != m_sourceJobMap.end())
    {
      for (const auto& itJob : itJobs->second)
      {
        if (itJob != nullptr)
          CJobQueue::CancelJob(itJob);
      }

      m_sourceJobMap.erase(sourceID);
      cancelled = true;
    }
  }

  if (cancelled)
    m_logger->debug("tasks for {} cancelled", sourceID);
}

void CMediaImportManager::CancelAllJobs()
{
  std::set<std::string> sources;
  {
    CSingleLock jobsLock(m_sourceJobsLock);
    for (const auto& itJobs : m_sourceJobMap)
      sources.insert(itJobs.first);
  }
  {
    CSingleLock jobsLock(m_importJobsLock);
    for (const auto& itJobs : m_importJobMap)
      sources.insert(itJobs.first);
  }

  for (const auto& source : sources)
    CancelJobs(source);
}

void CMediaImportManager::OnSourceJobComplete(const CMediaImportSourceJobBase* sourceJob,
                                              bool success)
{
  const std::string sourceJobType = sourceJob->GetType();
  if (sourceJobType == "MediaImportSourceRegistrationJob")
  {
    const auto sourceRegistrationTask =
        dynamic_cast<const CMediaImportSourceRegistrationJob*>(sourceJob);
    if (sourceRegistrationTask == nullptr)
      return;

    const auto& source = sourceRegistrationTask->GetSource();

    // nothing to do if the import job failed
    if (!success)
    {
      m_logger->warn("source registration task for {} failed", source);
      return;
    }

    if (!AddSourceSync(source))
    {
      m_logger->warn("source {} couldn't be added to any import repository", source);
      return;
    }
    OnSourceAdded(source);

    CServiceBroker::GetEventLog().AddWithNotification(
        EventPtr(new CMediaImportSourceEvent(source, 39550)));

    // check if the source should also be activated
    if (sourceRegistrationTask->ActivateSource())
    {
      if (!ActivateSourceSync(source, sourceRegistrationTask->IsSourceReady()))
      {
        m_logger->warn("source {} couldn't be activated", source);
        return;
      }
    }
  }
  else if (sourceJobType == "MediaImportSourceActivationJob")
  {
    const auto sourceActivationTask =
        dynamic_cast<const CMediaImportSourceActivationJob*>(sourceJob);
    if (sourceActivationTask == nullptr)
      return;

    const auto& source = sourceActivationTask->GetSource();

    // nothing to do if the import job failed
    if (!success)
    {
      m_logger->warn("source activation task for {} failed", source);
      return;
    }

    if (!ActivateSourceSync(source, sourceActivationTask->IsSourceReady()))
    {
      m_logger->warn("source {} couldn't be activated", source);
      return;
    }
  }
  else if (sourceJobType == "MediaImportSourceReadyJob")
  {
    const auto sourceReadyTask = dynamic_cast<const CMediaImportSourceReadyJob*>(sourceJob);
    if (sourceReadyTask == nullptr)
      return;

    const auto& source = sourceReadyTask->GetSource();

    // nothing to do if the import job failed
    if (!success)
    {
      m_logger->warn("source ready task for {} failed", source);
      return;
    }

    {
      CSingleLock sourcesLock(m_sourcesLock);
      auto&& itSource = m_sources.find(source.GetIdentifier());
      if (itSource == m_sources.end())
        return;

      itSource->second.ready = sourceReadyTask->IsSourceReady();
    }

    m_logger->debug("source ready for {} updated", source);
    OnSourceUpdated(source);
  }
  else
    m_logger->warn("unknown source job of type \"{}\" completed", sourceJobType);
}

void CMediaImportManager::OnJobComplete(unsigned int jobID, bool success, CJob* job)
{
  if (job == nullptr)
    return;

  std::string sourceID;
  auto taskProcessorJob = dynamic_cast<CMediaImportTaskProcessorJob*>(job);
  if (taskProcessorJob != nullptr)
  {
    sourceID = taskProcessorJob->GetPath();
    RemoveJob(taskProcessorJob->GetPath(), taskProcessorJob);
  }
  else
  {
    auto sourceJob = dynamic_cast<CMediaImportSourceJobBase*>(job);
    if (sourceJob == nullptr)
    {
      m_logger->warn("unknown job of type \"{}\" completed", job->GetType());
      return;
    }

    OnSourceJobComplete(sourceJob, success);

    const auto source = sourceJob->GetSource();
    UpdateSource(source);

    sourceID = source.GetIdentifier();
    RemoveJob(sourceID, sourceJob);
    CJobQueue::OnJobComplete(jobID, success, job);
  }

  if (!sourceID.empty())
  {
    CSingleLock sourcesLock(m_sourcesLock);
    // check if the source belonging to the completed process is being removed
    auto itSource = m_sources.find(sourceID);
    if (itSource != m_sources.end() && itSource->second.removing)
    {
      CMediaImportSource source(sourceID);
      if (FindSource(sourceID, source))
      {
        {
          CSingleLock repositoriesLock(m_importRepositoriesLock);
          for (auto& repository : m_importRepositories)
            repository->RemoveSource(sourceID);
        }

        m_sources.erase(itSource);
        sourcesLock.Leave();

        OnSourceRemoved(source);
        m_logger->debug("source {} removed", source);

        CServiceBroker::GetEventLog().Add(
            EventPtr(new CMediaImportSourceEvent(source, 39551, true)));
      }
    }
  }
}

void CMediaImportManager::OnJobProgress(unsigned int jobID,
                                        unsigned int progress,
                                        unsigned int total,
                                        const CJob* job)
{
  if (job == nullptr)
    return;

  if (strcmp(job->GetType(), "MediaImportTaskProcessorJob") == 0)
  {
    const CMediaImportTaskProcessorJob* processorJob =
        static_cast<const CMediaImportTaskProcessorJob*>(job);
    if (processorJob->GetCurrentTask() != nullptr &&
        processorJob->GetCurrentTask()->GetProgressBarHandle() != nullptr)
      processorJob->GetCurrentTask()->GetProgressBarHandle()->SetProgress(progress, total);
  }
}

void CMediaImportManager::OnTimeout()
{
  UpdateManuallyAddedSources();

  m_manuallyAddedSourceTimer.RestartAsync(MANUALLY_ADDED_SOURCE_INTERVAL_MS);
}

bool CMediaImportManager::OnTaskComplete(bool success, const IMediaImportTask* task)
{
  if (task == nullptr)
    return false;

  MediaImportTaskType taskType = task->GetType();
  if (taskType == MediaImportTaskType::ImportItemsRetrieval)
  {
    if (!success)
      return false;

    const auto* itemsRetrievalTask =
        dynamic_cast<const CMediaImportImportItemsRetrievalTask*>(task);
    if (itemsRetrievalTask == nullptr)
      return false;

    const auto& import = itemsRetrievalTask->GetImport();
    UpdateImport(import);
    UpdateSource(import.GetSource());
  }
  else if (taskType == MediaImportTaskType::Synchronisation)
  {
    const auto* synchronisationTask = dynamic_cast<const CMediaImportSynchronisationTask*>(task);
    if (synchronisationTask == nullptr)
      return false;

    auto import = synchronisationTask->GetImport();
    if (!success)
    {
      CServiceBroker::GetEventLog().Add(
          EventPtr(new CMediaImportEvent(import, 39553, EventLevel::Error)));
      return false;
    }

    // early return here if this is not the last media type to be synchronised to avoid multiple updates
    if (synchronisationTask->GetMediaType() != import.GetMediaTypes().back())
      return true;

    bool updated = false;
    {
      CSingleLock repositoriesLock(m_importRepositoriesLock);
      updated = std::any_of(m_importRepositories.begin(), m_importRepositories.end(),
                            [&import](MediaImportRepositoryPtr repository) {
                              return repository->UpdateLastSync(import);
                            });
    }

    if (updated)
    {
      OnSourceUpdated(import.GetSource());
      OnImportUpdated(import);
      CServiceBroker::GetEventLog().Add(EventPtr(new CMediaImportEvent(import, 39554)));
    }
  }
  else if (taskType == MediaImportTaskType::Removal)
  {
    const auto* removalTask = dynamic_cast<const CMediaImportRemovalTask*>(task);
    if (removalTask == nullptr)
      return false;

    const auto& import = removalTask->GetImport();
    if (!success)
    {
      CServiceBroker::GetEventLog().Add(
          EventPtr(new CMediaImportEvent(import, 39578, true, EventLevel::Error)));
      return false;
    }

    // remove the import from the import repositories
    {
      CSingleLock repositoriesLock(m_importRepositoriesLock);
      for (auto& repository : m_importRepositories)
        repository->RemoveImport(import);
    }

    // let everyone know that the import has been removed
    OnImportRemoved(import);

    CServiceBroker::GetEventLog().Add(EventPtr(new CMediaImportEvent(import, 39579, true)));
  }
  else if (taskType == MediaImportTaskType::Update)
  {
    if (!success)
      return false;

    const auto* updateTask = dynamic_cast<const CMediaImportUpdateTask*>(task);
    if (updateTask == nullptr)
      return false;

    const auto& import = updateTask->GetImport();
    UpdateImport(import);
    UpdateSource(import.GetSource());
  }

  return true;
}

void CMediaImportManager::OnSourceAdded(const CMediaImportSource& source)
{
  SendSourceMessage(source, GUI_MSG_SOURCE_ADDED);

  CSingleLock lock(m_importersLock);
  auto importer = GetImporterBySourceInternal(source.GetIdentifier());
  if (importer != m_importers.end())
    importer->second.observer->OnSourceAdded(source);
}

void CMediaImportManager::OnSourceUpdated(const CMediaImportSource& source)
{
  SendSourceMessage(source, GUI_MSG_SOURCE_UPDATED);

  CSingleLock lock(m_importersLock);
  auto importer = GetImporterBySourceInternal(source.GetIdentifier());
  if (importer != m_importers.end())
    importer->second.observer->OnSourceUpdated(source);
}

void CMediaImportManager::OnSourceRemoved(const CMediaImportSource& source)
{
  SendSourceMessage(source, GUI_MSG_SOURCE_REMOVED);

  CSingleLock lock(m_importersLock);
  auto importer = GetImporterBySourceInternal(source.GetIdentifier());
  if (importer != m_importers.end())
    importer->second.observer->OnSourceRemoved(source);
}

void CMediaImportManager::OnSourceActivated(const CMediaImportSource& source)
{
  SendSourceMessage(source, GUI_MSG_SOURCE_ACTIVE_CHANGED, 1);

  CSingleLock lock(m_importersLock);
  auto importer = GetImporterBySourceInternal(source.GetIdentifier());
  if (importer != m_importers.end())
    importer->second.observer->OnSourceActivated(source);
}

void CMediaImportManager::OnSourceDeactivated(const CMediaImportSource& source)
{
  SendSourceMessage(source, GUI_MSG_SOURCE_ACTIVE_CHANGED, 0);

  CSingleLock lock(m_importersLock);
  auto importer = GetImporterBySourceInternal(source.GetIdentifier());
  if (importer != m_importers.end())
    importer->second.observer->OnSourceDeactivated(source);
}

void CMediaImportManager::OnImportAdded(const CMediaImport& import)
{
  SendImportMessage(import, GUI_MSG_IMPORT_ADDED);

  CSingleLock lock(m_importersLock);
  auto importer = GetImporterBySourceInternal(import.GetSource().GetIdentifier());
  if (importer != m_importers.end())
    importer->second.observer->OnImportAdded(import);
}

void CMediaImportManager::OnImportUpdated(const CMediaImport& import)
{
  SendImportMessage(import, GUI_MSG_IMPORT_UPDATED);

  CSingleLock lock(m_importersLock);
  auto importer = GetImporterBySourceInternal(import.GetSource().GetIdentifier());
  if (importer != m_importers.end())
    importer->second.observer->OnImportUpdated(import);
}

void CMediaImportManager::OnImportRemoved(const CMediaImport& import)
{
  SendImportMessage(import, GUI_MSG_IMPORT_REMOVED);

  CSingleLock lock(m_importersLock);
  auto importer = GetImporterBySourceInternal(import.GetSource().GetIdentifier());
  if (importer != m_importers.end())
    importer->second.observer->OnImportRemoved(import);
}

void CMediaImportManager::SendSourceMessage(const CMediaImportSource& source,
                                            int message,
                                            int param /* = 0 */)
{
  CFileItemPtr sourceItem(new CFileItem(source.GetFriendlyName()));
  sourceItem->SetProperty("Source.ID", source.GetIdentifier());

  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, message, param, sourceItem);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
}

void CMediaImportManager::SendImportMessage(const CMediaImport& import, int message)
{
  CFileItemPtr importItem(new CFileItem());
  importItem->SetProperty("Source.ID", import.GetSource().GetIdentifier());
  importItem->SetProperty("Import.Path", import.GetPath());
  importItem->SetProperty("Import.MediaTypes", CMediaTypes::Join(import.GetMediaTypes()));

  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, message, 0, importItem);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
}
