/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/MediaImport.h"
#include "media/import/MediaImportSource.h"
#include "utils/ILocalizer.h"

#include <memory>
#include <string>

class CMediaImportImportItemsRetrievalTask;
class CMediaImportUpdateTask;

/*!
 * \brief Base innterface of a media importer capable of importing media items from a specific source into
 * the local library.
 */
class IMediaImporterBase
{
public:
  virtual ~IMediaImporterBase() = default;

  /*!
   * \brief Gets a unique identification of the media importer
   */
  virtual const char* GetIdentification() const = 0;

  /*!
   * \brief Checks if the implementation can manually lookup media sources.
   */
  virtual bool CanLookupSource() const { return false; }

  /*!
   * \brief Provide a protocol of the media importer for manual media source lookup.
   */
  virtual std::string GetSourceLookupProtocol() const { return ""; }
};

/*!
 * \brief TODO
 */
class IMediaImporterDiscoverer : public virtual IMediaImporterBase
{
public:
  virtual ~IMediaImporterDiscoverer() = default;

  /*!
   * \brief Starts any importer specific tasks/actions.
   */
  virtual void Start() {}
};

/*!
 * \brief TODO
 */
class IMediaImporter : public virtual IMediaImporterBase, public ILocalizer
{
public:
  virtual ~IMediaImporter() = default;

  // implementation of ILocalizer
  std::string Localize(std::uint32_t code) const override { return ""; }

  /*!
   * \brief Tries to discover / find a new media source.
   *
   * \param source The looked up source.
   * \return True if a source was found, false otherwise.
   */
  virtual bool DiscoverSource(CMediaImportSource& source) = 0;

  /*!
   * \brief Tries to lookup / find the given media source.
   *
   * \param source The source to lookup.
   * \return True if the given source was found, false otherwise.
   */
  virtual bool LookupSource(const CMediaImportSource& source) = 0;

  /*
   * \brief Checks if the implementation can import items from the given path.
   *
   * ATTENTION: This method can be called with a path that does not match the
   *            path of the import used to create the importer.
   *
   * \param path Path to a source or import
   * \return True if the implementation can import items from the given path, false otherwise
   */
  virtual bool CanImport(const std::string& path) = 0;

  /*
   * \brief Checks if the source is ready for importing.
   *
   * \return True if the source is ready for importing, false otherwise
   */
  virtual bool IsSourceReady(CMediaImportSource& source) = 0;

  /*
   * \brief Checks if the import is ready to be processed.
   *
   * \return True if the import is ready to be processed, false otherwise
   */
  virtual bool IsImportReady(CMediaImport& import) = 0;

  /*
   * \brief Prepares and loads the source's settings based on the importers functionality.
   *
   * The caller needs to guarantee that the given CMediaImportSource instance stays
   * valid until UnloadSourceSettings() has been called with the same instance.
   *
   * \return True if the source's settings have been successfully prepared and loaded, false otherwise
   */
  virtual bool LoadSourceSettings(CMediaImportSource& source) { return false; }
  /*
   * \brief Saves and unloads the source's settings based on the importers functionality.
   *
   * \return True if the source's settings have been successfully saved and unloaded, false otherwise
   */
  virtual bool UnloadSourceSettings(CMediaImportSource& source) { return false; }

  /*
   * \brief Prepares and loads the import's settings based on the importers functionality.
   *
   * The caller needs to guarantee that the given CMediaImport instance stays
   * valid until UnloadImportSettings() has been called with the same instance.
   *
   * \return True if the import's settings have been successfully prepared and loaded, false otherwise
   */
  virtual bool LoadImportSettings(CMediaImport& import) { return false; }
  /*
   * \brief Saves and unloads the import's settings based on the importers functionality.
   *
   * \return True if the import's settings have been successfully saved and unloaded, false otherwise
   */
  virtual bool UnloadImportSettings(CMediaImport& import) { return false; }

  /*!
   * \brief Checks if the implementation can update general metadata of an
   * imported item on the source with the given path.
   *
   * \param path Path to a source or import
   * \return True if the implementation can update general metadata of an imported item on the source with the given path, false otherwise
   */
  virtual bool CanUpdateMetadataOnSource(const std::string& path) { return false; }

  /*!
   * \brief Checks if the implementation can update the playcount of an
   * imported item on the source with the given path.
   *
   * \param path Path to a source or import
   * \return True if the implementation can update the playcount of an imported item on the source with the given path, false otherwise
   */
  virtual bool CanUpdatePlaycountOnSource(const std::string& path) { return false; }

  /*!
   * \brief Checks if the implementation can update the last played date of an
   * imported item on the source with the given path.
   *
   * \param path Path to a source or import
   * \return True if the implementation can update the last played date of an imported item on the source with the given path, false otherwise
   */
  virtual bool CanUpdateLastPlayedOnSource(const std::string& path) { return false; }

  /*!
   * \brief Checks if the implementation can update the resume position of an
   * imported item on the source with the given path.
   *
   * \param path Path to a source or import
   * \return True if the implementation can update the resume position of an imported item on the source with the given path, false otherwise
   */
  virtual bool CanUpdateResumePositionOnSource(const std::string& path) { return false; }

  /*!
   * \brief Imports items from the source.
   *
   * The imported items are stored in the given CMediaImportRetrievalTask instance by
   * their respective media type.
   *
   * \param task Task performing the import (to check if we need to cancel import and to report progress)
   * \return True if the import succeeded, false otherwise
   */
  virtual bool Import(CMediaImportImportItemsRetrievalTask* task) = 0;

  /*!
   * \brief Updates an item's metadata on the source
   *
   * The given CMediaImportUpdateTask contains a media item whose metadata
   * should be updated on the source if it is supported by the importer.
   *
   * \param task Task performing the updating
   * \return True if updating the item's metadata succeeded, false otherwise
   */
  virtual bool UpdateOnSource(CMediaImportUpdateTask* task) = 0;
};

/*!
 * \brief TODO
 */
class IMediaImporterObserver
{
public:
  virtual ~IMediaImporterObserver() = default;

  /*!
   * \brief TODO
   */
  virtual void Start() {}

  /*!
   * \brief TODO
   */
  virtual void OnSourceAdded(const CMediaImportSource& source) {}

  /*!
   * \brief TODO
   */
  virtual void OnSourceUpdated(const CMediaImportSource& source) {}

  /*!
   * \brief TODO
   */
  virtual void OnSourceRemoved(const CMediaImportSource& source) {}

  /*!
   * \brief TODO
   */
  virtual void OnSourceActivated(const CMediaImportSource& source) {}

  /*!
   * \brief TODO
   */
  virtual void OnSourceDeactivated(const CMediaImportSource& source) {}

  /*!
   * \brief TODO
   */
  virtual void OnImportAdded(const CMediaImport& import) {}

  /*!
   * \brief TODO
   */
  virtual void OnImportUpdated(const CMediaImport& import) {}

  /*!
   * \brief TODO
   */
  virtual void OnImportRemoved(const CMediaImport& import) {}

protected:
  IMediaImporterObserver() = default;
};

/*!
 * \brief TODO
 */
class IMediaImporterFactory
{
public:
  virtual ~IMediaImporterFactory() = default;

  /*!
   * \brief Gets a unique identification of the media importer
   */
  virtual const char* GetIdentification() const = 0;

  /*!
  * \brief Creates a new IMediaImporterDiscoverer instance of the implementation.
  *
  * \return New IMediaImporterDiscoverer instance of the implementation
  */
  virtual std::unique_ptr<IMediaImporterDiscoverer> CreateDiscoverer() const = 0;

  /*!
  * \brief Creates a new IMediaImporter instance of the implementation.
  *
  * \return New IMediaImporter instance of the implementation
  */
  virtual std::unique_ptr<IMediaImporter> CreateImporter() const = 0;

  /*!
  * \brief TODO
  *
  * \return New IMediaImporterObserver instance of the implementation
  */
  virtual std::unique_ptr<IMediaImporterObserver> CreateObserver() const = 0;
};

using MediaImporterDiscovererPtr = std::unique_ptr<IMediaImporterDiscoverer>;
using MediaImporterPtr = std::unique_ptr<IMediaImporter>;
using MediaImporterObserverPtr = std::unique_ptr<IMediaImporterObserver>;
using MediaImporterFactoryPtr = std::shared_ptr<IMediaImporterFactory>;
using MediaImporterFactoryConstPtr = std::shared_ptr<const IMediaImporterFactory>;
