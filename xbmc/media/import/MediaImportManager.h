/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/MediaType.h"
#include "media/import/IMediaImportHandler.h"
#include "media/import/IMediaImportHandlerManager.h"
#include "media/import/IMediaImportRepository.h"
#include "media/import/IMediaImporter.h"
#include "media/import/IMediaImporterManager.h"
#include "media/import/MediaImportSource.h"
#include "media/import/jobs/tasks/IMediaImportTask.h"
#include "threads/CriticalSection.h"
#include "threads/Timer.h"
#include "utils/JobManager.h"
#include "utils/logtypes.h"

#include <map>
#include <set>
#include <vector>

class CFileItem;
class CGUIDialogProgress;
class CMediaImportSourceJobBase;
class CMediaImportTaskProcessorJob;

class CMediaImportManager : public virtual IJobCallback,
                            public ITimerCallback,
                            public IMediaImportTaskCallback,
                            public IMediaImporterManager,
                            public IMediaImportHandlerManager,
                            protected CJobQueue
{
public:
  CMediaImportManager();
  CMediaImportManager(const CMediaImportManager&) = delete;
  CMediaImportManager const& operator=(CMediaImportManager const&) = delete;
  ~CMediaImportManager();

  /*!
  * \brief Initializes the media manager by registering all importers and import handlers.
  */
  void Initialize();

  /*!
   * \brief Uninitializes the media manager by releasing all importers, import handlers and repositories.
   */
  void Uninitialize();

  /*!
   * \brief Register a media import repository implementation
   *
   * \param importRepository media import repository implementation to register
   */
  void RegisterImportRepository(MediaImportRepositoryPtr importRepository);
  /*!
   * \brief Unregister a media import repository implementation
   *
   * \param importRepository media import repository implementation to unregister
   * \return True if the media import repository implementation was unregistered, false otherwise
   */
  bool UnregisterImportRepository(const MediaImportRepositoryPtr importRepository);

  /*!
   * \brief Register a media importer implementation
   *
   * \param importer media importer implementation to register
   */
  void RegisterImporter(MediaImporterFactoryPtr importer);
  /*!
   * \brief Unregister a media importer implementation
   *
   * \param importerIdentifier identifier of the media importer to unregister
   * \return True if the importer was unregistered, false otherwise
   */
  bool UnregisterImporter(const std::string& importerIdentifier);

  /*!
   * \brief Returns a list of registered media importer implementations.
   */
  std::vector<MediaImporterFactoryConstPtr> GetImporters() const;

  /*!
   * \brief Tries to discover / find a new media source using the given importer.
   *
   * \param importerIdentification Identification of the importer to use for finding the new source.
   * \param source The found source.
   * \return True if a source was found, false otherwise.
   */
  bool DiscoverSource(const std::string& importerIdentification, CMediaImportSource& source);

  // implementations of IMediaImporterManager
  bool HasImporter(const std::string& id) const override;
  MediaImporterPtr GetImporterById(const std::string& id) const override;
  MediaImporterPtr GetImporterBySource(const CMediaImportSource& source) const override;

  /*!
   * \brief Register a media import handler implementation
   *
   * \param importHandler media import handler implementation to register
   */
  void RegisterMediaImportHandler(MediaImportHandlerPtr importHandler);
  /*!
   * \brief Unregister a media import handler implementation
   *
   * \param importHandler media import handler implementation to unregister
   */
  void UnregisterMediaImportHandler(MediaImportHandlerPtr importHandler);

  // implementation of IMediaImportHandlerManager
  MediaImportHandlerConstPtr GetImportHandler(const MediaType& mediaType) const override;

  /*!
   * \brief Returns a list of registered media import handler implementations.
   */
  std::vector<MediaImportHandlerConstPtr> GetImportHandlers() const;

  /*!
   * \brief Returns a list of registered media import handler implementations
   * capable of handling imported media items of one of the given media types.
   *
   * \param mediaTypes Media types of imported items
   * \pram List of media import handler implementations
   */
  std::vector<MediaImportHandlerConstPtr> GetImportHandlers(const MediaTypes& mediaTypes) const;

  /*!
   * \brief Checks if the given media type is supported.
   */
  bool IsMediaTypeSupported(const MediaType& mediaType) const;

  /*!
   * \brief Returns a list of all supported media types.
   */
  MediaTypes GetSupportedMediaTypes() const;

  /*!
   * \brief Returns a list of all supported media types in the order they need to be synchronised.
   *
   * \param reversed Whether the list should be reversed or not
   */
  GroupedMediaTypes GetSupportedMediaTypesOrdered(bool reversed = false) const;

  /*!
   * \brief Returns a list of the given media types in the order they need to be synchronised.
   *
   * \param mediaTypes List of media types to be ordered
   * \param reversed Whether the list should be reversed or not
   */
  GroupedMediaTypes GetSupportedMediaTypesOrdered(const MediaTypes& mediaTypes,
                                                  bool reversed = false) const;

  /*!
   * \brief Returns a list of the given media types with depending media types grouped together.
   *
   * \param mediaTypes List of media types to be grouped
   */
  std::vector<GroupedMediaTypes> GetSupportedMediaTypesGrouped(const MediaTypes& mediaTypes) const;

  /*!
   * \brief Returns a set of all media types grouped together with the given media type.
   */
  GroupedMediaTypes GetGroupedMediaTypes(const MediaType& mediaType) const;

  /*!
   * \brief Adds a discovered source asynchronously
   *
   * \param importerId Unique identifier of the importer used to add the source
   * \param sourceID Unique identifier of the source
   * \param basePath Base path (VFS path) of the source
   * \param friendlyName Friendly name of the source
   * \param iconUrl URL to the icon of the source
   * \param mediaTypes Media types supported by the source
   */
  bool AddSource(const std::string& importerId,
                 const std::string& sourceID,
                 const std::string& basePath,
                 const std::string& friendlyName,
                 const std::string& iconUrl = "",
                 const MediaTypes& mediaTypes = MediaTypes());
  bool AddSource(const CMediaImportSource& source);

  /*!
   * \brief Adds and activates a discovered source asynchronously
   *
   * \param importerId Unique identifier of the importer used to add the source
   * \param sourceID Unique identifier of the source
   * \param basePath Base path (VFS path) of the source
   * \param friendlyName Friendly name of the source
   * \param iconUrl URL to the icon of the source
   * \param mediaTypes Media types supported by the source
   */
  bool AddAndActivateSource(const std::string& importerId,
                            const std::string& sourceID,
                            const std::string& basePath,
                            const std::string& friendlyName,
                            const std::string& iconUrl = "",
                            const MediaTypes& mediaTypes = MediaTypes());
  bool AddAndActivateSource(const CMediaImportSource& source);

  /*!
   * \brief Adds a manuallyAdded looked up source asynchronously
   *
   * \param importerId Unique identifier of the importer used to add the source
   * \param sourceID Unique identifier of the source
   * \param basePath Base path (VFS path) of the source
   * \param friendlyName Friendly name of the source
   * \param iconUrl URL to the icon of the source
   * \param mediaTypes Media types supported by the source
   */
  bool AddSourceManually(const std::string& importerId,
                         const std::string& sourceID,
                         const std::string& basePath,
                         const std::string& friendlyName,
                         const std::string& iconUrl = "",
                         const MediaTypes& mediaTypes = MediaTypes());
  bool AddSourceManually(const CMediaImportSource& source);

  /*!
   * \brief Adds and activates a manuallyAdded looked up source asynchronously
   *
   * \param importerId Unique identifier of the importer used to add the source
   * \param sourceID Unique identifier of the source
   * \param basePath Base path (VFS path) of the source
   * \param friendlyName Friendly name of the source
   * \param iconUrl URL to the icon of the source
   * \param mediaTypes Media types supported by the source
   */
  bool AddAndActivateSourceManually(const std::string& importerId,
                                    const std::string& sourceID,
                                    const std::string& basePath,
                                    const std::string& friendlyName,
                                    const std::string& iconUrl = "",
                                    const MediaTypes& mediaTypes = MediaTypes());
  bool AddAndActivateSourceManually(const CMediaImportSource& source);

  /*!
   * \brief Activates a discovered source
   * If the source is already known all imports from that source are being
   * triggered.
   *
   * \param sourceID Unique identifier (VFS path) of the source
   * \param basePath Base path (VFS path) of the source
   * \param friendlyName Friendly name of the source
   * \param iconUrl URL to the icon of the source
   * \return True if the source was successfully activated, false otherwise
   */
  bool ActivateSource(const std::string& importerId,
                      const std::string& sourceID,
                      const std::string& basePath = "",
                      const std::string& friendlyName = "",
                      const std::string& iconUrl = "");
  bool ActivateSource(const CMediaImportSource& source);

  /*!
   * \brief Deactivates the source with the given identifier
   * All items imported from the source are being marked as disabled.
   *
   * \param sourceID Unique identifier of the source
   */
  void DeactivateSource(const std::string& sourceID);

  /*!
  * \brief Updates the details and settings of the given source.
  *
  * \param source Updated source
  * \return True if the source was successfully updated, false otherwise
  */
  bool UpdateSource(const CMediaImportSource& source);

  /*!
   * \brief Completely removes the source with the given identifier
   * Removes the source, all its imports and all items imported from the
   * source from the libraries.
   *
   * \param sourceID Unique identifier of the source
   */
  void RemoveSource(const std::string& sourceID);

  /*!
   * \brief Whether any sources have been registered.
   *
   * \return True if at least one source has been registered, false otherwise.
   */
  bool HasSources() const;

  /*!
   * \brief Whether any active/inactive sources have been registered.
   *
   * \return True if at least one active/inactive source has been registered, false otherwise.
   */
  bool HasSources(bool active) const;

  /*!
   * \brief Gets all registered sources
   *
   * \return Set of all registered sources
   */
  std::vector<CMediaImportSource> GetSources() const;

  /*!
   * \brief Gets all active or inactive registered sources
   *
   * \param active Whether to get active or inactive sources
   * \return Set of all active or inactive registered sources
   */
  std::vector<CMediaImportSource> GetSources(bool active) const;

  /*!
   * \brief Gets the source with the given identifier.
   *
   * \param sourceID Unique identifier of the source
   * \param source Source with the given identifier
   * \return True if the source with the given identifier was found, false otherwise
   */
  bool GetSource(const std::string& sourceID, CMediaImportSource& source) const;

  /*!
   * \brief Checks whether the given source is active or not.
   *
   * \param sourceID Unique identifier of the source
   * \return True if the given source is active, otherwise false
   */
  bool IsSourceActive(const std::string& sourceID) const;

  /*!
   * \brief Checks whether the given source is active or not.
   *
   * \param source Source object
   * \return True if the given source is active, otherwise false
   */
  bool IsSourceActive(const CMediaImportSource& source) const;

  /*
  * \brief Checks whether the given source is ready for importing.
  *
  * \param sourceID Unique identifier of the source to be checked for readiness
  * \return True if the given source is ready for importing, false otherwise
  */
  bool IsSourceReady(const std::string& sourceID) const;

  /*
  * \brief Checks whether the given source is ready for importing.
  *
  * \param source Source to be checked for readiness
  * \return True if the given source is ready for importing, false otherwise
  */
  bool IsSourceReady(const CMediaImportSource& source) const;

  /*!
   * \brief Whether any imports have been defined.
   *
   * \return True if at least one import has been defined, false otherwise.
   */
  bool HasImports() const;

  /*!
   * \brief Whether the source with the given identifier has imports defined.
   *
   * \param sourceID Identifier of a source
   * \return True if the source with the given identifier has imports defined, false otherwise.
   */
  bool HasImports(const std::string& sourceID) const;

  /*!
   * \brief Whether the given source has imports defined.
   *
   * \param source Source
   * \return True if the given source has imports defined, false otherwise.
   */
  bool HasImports(const CMediaImportSource& source) const;

  /*!
   * \brief Adds a new selective import to the given source for the given path and media type.
   *
   * Selective importing means that the given import path must point to a single
   * item to be imported.
   *
   * \param sourceID Source identifier
   * \param path Path from where to import media items
   * \param mediaTypes Types of the media items to import
   * \return True if the import was successfully added, false otherwise
   */
  bool AddSelectiveImport(const std::string& sourceID,
                          const std::string& path,
                          const GroupedMediaTypes& mediaTypes);

  /*!
   * \brief Adds a new recursive import to the given source for the given path and media type.
   *
   * Recursive importing means that the given import path must be a path to a
   * directory which can be listed.
   *
   * \param sourceID Source identifier
   * \param path Path from where to import media items
   * \param mediaTypes Types of the media items to import
   * \return True if the import was successfully added, false otherwise
   */
  bool AddRecursiveImport(const std::string& sourceID,
                          const std::string& path,
                          const GroupedMediaTypes& mediaTypes);

  /*!
   * \brief Adds new recursive imports to the given source for the given path and media types.
   *
   * \param sourceID Source identifier
   * \param path Path from where to import media items
   * \param mediaTypes Set of types of the media items to import
   * \return True if the imports were successfully added, false otherwise
   */
  bool AddRecursiveImports(const std::string& sourceID,
                           const std::string& path,
                           const std::set<GroupedMediaTypes>& mediaTypes);

  /*!
   * \brief Updates the details and settings of the given import.
   *
   * \param import Updated import
   * \return True if the import was successfully updated, false otherwise
   */
  bool UpdateImport(const CMediaImport& import);

  /*!
   * \brief Completely removes the import with the given path and media type.
   * 
   * \details Removes the import and all items imported from the import from the libraries.
   *
   * \param path Path of the import
   * \param mediaTypes Media types of the import
   */
  void RemoveImport(const std::string& path, const GroupedMediaTypes& mediaTypes);

  /*!
   * \brief Returns a list of all registered media imports.
   */
  std::vector<CMediaImport> GetImports() const;

  /*!
   * \brief Returns a list of all registered media imports for the given media type.
   *
   * \param mediaTypes Media types of the imports
   */
  std::vector<CMediaImport> GetImportsByMediaType(const GroupedMediaTypes& mediaTypes) const;

  /*!
   * \brief Returns a list of media imports belonging to the source with the given identifier.
   *
   * \param sourceID Source identifier
   */
  std::vector<CMediaImport> GetImportsBySource(const std::string& sourceID) const;

  /*!
   * \brief Returns a list of media imports belonging to the given path.
   *
   * \param path Path of the imports
   * \param includeSubDirectories Whether to include subdirectories or not
   */
  std::vector<CMediaImport> GetImportsByPath(const std::string& path,
                                             bool includeSubDirectories = false) const;

  /*!
   * \brief Gets the import for the given path and media type.
   *
   * \param path Path of the import
   * \param mediaTypes Media types of the import
   * \return True if the import for the given path and media type was found, false otherwise
   */
  bool GetImport(const std::string& path,
                 const GroupedMediaTypes& mediaTypes,
                 CMediaImport& import) const;

  /*
  * \brief Checks if the given import is ready to be processed.
  *
  * \param path Path of the import to be checked for readiness
   * \param mediaTypes Media types of the import to be checked for readiness
  * \return True if the given import is ready to be processed, false otherwise
  */
  bool IsImportReady(const std::string& path, const GroupedMediaTypes& mediaTypes) const;

  /*
  * \brief Checks if the given import is ready to be processed.
  *
  * \param import Import to be checked for readiness
  * \return True if the given import is ready to be processed, false otherwise
  */
  bool IsImportReady(const CMediaImport& import) const;

  /*!
  * \brief Checks if the given path is imported (ignoring parent paths).
  *
  * \return True if the given path is imported, false otherwise
  */
  bool IsImported(const std::string& path) const;

  /*!
   * \brief Checks if the given path or any of its parent paths is imported.
   *
   * \return True if the given path is imported, false otherwise
   */
  bool IsImportedInHierarchy(const std::string& path) const;

  /*!
   * \brief Import media items from all registered sources and imports.
   *
   * \return True if the import of media items has been started, false otherwise
   */
  bool Import();

  /*!
   * \brief Import media items from the given source.
   *
   * \param sourceID Unique identifier of the source
   * \return True if the import of media items has been started, false otherwise
   */
  bool Import(const std::string& sourceID);

  /*!
   * \brief Import media items of the given media type from the given path.
   *
   * \param path Path from where media items will be imported
   * \param mediaTypes media types to import
   * \return True if the import of media items has been started, false otherwise
   */
  bool Import(const std::string& path, const GroupedMediaTypes& mediaTypes);

  /*!
   * \brief Adds the given items as imported items from the given media import.
   *
   * \param import Media import the given item has been imported from
   * \param items Newly imported items
   * \return True if the import of the items has been started, false otherwise
   */
  bool AddImportedItems(const CMediaImport& import, const CFileItemList& item);

  /*!
  * \brief Updates the given items imported from the given media import.
   *
   * \param import Media import the given item has been imported from
   * \param items Updated imported items
   * \return True if the update of the imported items has been started, false otherwise
  */
  bool UpdateImportedItems(const CMediaImport& import, const CFileItemList& items);

  /*!
  * \brief Removes the given items previously imported from the given media import.
   *
   * \param import Media import the given item has been imported from
   * \param items Removed imported items
   * \return True if the removal of the imported items has been started, false otherwise
  */
  bool RemoveImportedItems(const CMediaImport& import, const CFileItemList& items);

  /*!
  * \brief changes the given items imported from the given media import.
  *
  * \param import Media import the given item has been imported from
  * \param items Changed imported items
  * \return True if the change of the imported items has been started, false otherwise
  */
  bool ChangeImportedItems(const CMediaImport& import, const ChangesetItems& items);

  /*!
   * \brief Updates the details of the imported media item on the source from where it was imported.
   *
   * \param item Imported media item to update on the source
   * \return True if the imported media item was successfully updated on the source, false otherwise
   */
  bool UpdateImportedItemOnSource(const CFileItem& item);

  /*!
   * \brief TODO
   */
  std::vector<CFileItemPtr> GetImportedItemsBySource(const CMediaImportSource& source) const;

  /*!
   * \brief TODO
   */
  std::vector<CFileItemPtr> GetImportedItemsByImport(const CMediaImport& import) const;

  // implementation of IJobCallback
  void OnJobComplete(unsigned int jobID, bool success, CJob* job) override;
  void OnJobProgress(unsigned int jobID,
                     unsigned int progress,
                     unsigned int total,
                     const CJob* job) override;

  // implementation of ITimerCallback
  void OnTimeout() override;

  // implementation of IMediaImportTaskCallback
  bool OnTaskComplete(bool success, const IMediaImportTask* task) override;

private:
  typedef struct MediaImporter
  {
    MediaImporterFactoryPtr factory;
    MediaImporterDiscovererPtr discoverer;
    MediaImporterObserverPtr observer;
  } MediaImporter;
  using MediaImporterMap = std::map<std::string, MediaImporter>;

  MediaImporterMap::iterator GetImporterBySourceInternal(const std::string& sourceId);

  void PrepareImports(std::vector<CMediaImport>& imports) const;

  bool AddSourceAsync(CMediaImportSource source, bool activate, bool manuallyAdded);
  bool AddSourceSync(const CMediaImportSource& source);
  bool ActivateSourceAsync(const CMediaImportSource& source);
  bool ActivateSourceSync(const CMediaImportSource& source, bool ready);
  bool GetSourceInternal(const std::string& sourceID, CMediaImportSource& source) const;
  bool FindSource(const std::string& sourceID, CMediaImportSource& source) const;
  void UpdateManuallyAddedSources();
  bool LookupSource(const CMediaImportSource& source);

  bool AddImport(const std::string& sourceID,
                 const std::string& path,
                 const GroupedMediaTypes& mediaTypes,
                 bool recursive);
  bool AddImport(const CMediaImport& import);
  bool FindImport(const std::string& path,
                  const GroupedMediaTypes& mediaTypes,
                  CMediaImport& import) const;

  void AddJob(const std::string& sourceID, CMediaImportSourceJobBase* job);
  void AddJob(const std::string& sourceID, CMediaImportTaskProcessorJob* job);
  void RemoveJob(const std::string& sourceID, const CMediaImportSourceJobBase* job);
  void RemoveJob(const std::string& sourceID, const CMediaImportTaskProcessorJob* job);
  void CancelJobs(const std::string& sourceID);
  void CancelAllJobs();

  void OnSourceJobComplete(const CMediaImportSourceJobBase* sourceJob, bool success);

  void Import(const CMediaImportSource& source, bool automatically = false);
  void Import(const CMediaImport& import, bool automatically = false);

  void OnSourceAdded(const CMediaImportSource& source);
  void OnSourceUpdated(const CMediaImportSource& source);
  void OnSourceRemoved(const CMediaImportSource& source);
  void OnSourceActivated(const CMediaImportSource& source);
  void OnSourceDeactivated(const CMediaImportSource& source);
  void OnImportAdded(const CMediaImport& import);
  void OnImportUpdated(const CMediaImport& import);
  void OnImportRemoved(const CMediaImport& import);
  void SendSourceMessage(const CMediaImportSource& source, int message, int param = 0);
  void SendImportMessage(const CMediaImport& import, int message);

  Logger m_logger;

  bool m_initialized;

  static const uint32_t MANUALLY_ADDED_SOURCE_INTERVAL_MS = 60 * 1000;
  CTimer m_manuallyAddedSourceTimer;

  mutable CCriticalSection m_importRepositoriesLock;
  std::set<MediaImportRepositoryPtr> m_importRepositories;

  typedef struct MediaImportSource
  {
    std::string importerId;
    bool active;
    bool ready;
    bool removing;
  } MediaImportSource;

  mutable CCriticalSection m_sourcesLock;
  std::map<std::string, MediaImportSource> m_sources;

  mutable CCriticalSection m_importersLock;
  std::map<std::string, MediaImporter> m_importers;

  mutable CCriticalSection m_importHandlersLock;
  std::map<MediaType, MediaImportHandlerConstPtr> m_importHandlersMap;
  std::vector<MediaImportHandlerConstPtr> m_importHandlers;

  mutable CCriticalSection m_sourceJobsLock;
  std::map<std::string, std::set<CMediaImportSourceJobBase*>> m_sourceJobMap;
  mutable CCriticalSection m_importJobsLock;
  std::map<std::string, std::set<CMediaImportTaskProcessorJob*>> m_importJobMap;
};
