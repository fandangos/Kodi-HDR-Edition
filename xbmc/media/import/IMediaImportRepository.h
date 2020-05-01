/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/MediaType.h"
#include "media/import/MediaImport.h"
#include "media/import/MediaImportSource.h"

#include <memory>
#include <vector>

class CFileItem;

/*!
 * \brief Interface defining a repository capable of storing media items imported from sources and their imports.
 */
class IMediaImportRepository
{
public:
  virtual ~IMediaImportRepository() = default;

  /*
   * \brief Initializes the repository.
   *
   * \return True if the initialization was successful, false otherwise
   */
  virtual bool Initialize() = 0;

  /*
   * \brief Gets all imports stored in the repository.
   *
   * \return List of imports
   */
  virtual std::vector<CMediaImport> GetImports() const = 0;

  /*
   * \brief Gets all imports from the source with the given identifier stored in the repository.
   *
   * \param sourceIdentifier Source identifier
   * \return List of imports
   */
  virtual std::vector<CMediaImport> GetImportsBySource(
      const std::string& sourceIdentifier) const = 0;

  /*
   * \brief Gets all imports for the given media types stored in the repository.
   *
   * \param mediaTypes Media types
   * \return List of imports
   */
  virtual std::vector<CMediaImport> GetImportsByMediaType(
      const GroupedMediaTypes& mediaTypes) const = 0;

  /*!
   * \brief Gets all imports for the given path stored in the repository.
   *
   * \param path Path of the imports
   * \param includeSubDirectories Whether to include subdirectories or not
   */
  virtual std::vector<CMediaImport> GetImportsByPath(const std::string& path,
                                                     bool includeSubDirectories = false) const = 0;

  /*
   * \brief Gets the import for the given path and media type.
   *
   * \param path Path of the import
   * \param mediaTypes Media types of the items imported from the import
   * \param import[out] Import
   * \return True if a matching import was found, false otherwise
   */
  virtual bool GetImport(const std::string& path,
                         const GroupedMediaTypes& mediaTypes,
                         CMediaImport& import) const = 0;

  /*
   * \brief Adds the given import to the repository.
   *
   * \param import Import to be added
   * \param added[out] Whether the import has been added to the repository or not.
   * \return True if the import was successfully added, false otherwise
   */
  virtual bool AddImport(const CMediaImport& import, bool& added) = 0;

  /*
   * \brief Updates the given import in the repository.
   *
   * \param import Import to be updated
   * \param updated[out] Whether the import has been updated in the repository or not.
   * \return True if the import was successfully updated, false otherwise
   */
  virtual bool UpdateImport(const CMediaImport& import, bool& updated) = 0;

  /*
   * \brief Removes the given import from the repository.
   *
   * \param import Import to be removed
   * \return True if the import was successfully removed, false otherwise
   */
  virtual bool RemoveImport(const CMediaImport& import) = 0;

  /*
   * \brief Updates the last synchronisation timestamp of the given import in the repository.
   *
   * \param import Import to be updated
   * \return True if the import was successfully updated, false otherwise
   */
  virtual bool UpdateLastSync(CMediaImport& import) = 0;

  /*
   * \brief Gets all sources supporting the given media types stored in the repository.
   *
   * \param mediaTypes Media types
   * \return List of sources
   */
  virtual std::vector<CMediaImportSource> GetSources(
      const GroupedMediaTypes& mediaTypes = GroupedMediaTypes()) const = 0;

  /*
   * \brief Gets the source with the given identifier.
   *
   * \param identifier Source identifier
   * \param source[out] Source
   * \return True if a matching source was found, false otherwise
   */
  virtual bool GetSource(const std::string& identifier, CMediaImportSource& source) const = 0;

  /*
   * \brief Adds the given source to the repository.
   *
   * \param source Source to be added
   * \param added[out] Whether the source has been added to the repository or not.
   * \return True if the source was successfully added, false otherwise
   */
  virtual bool AddSource(const CMediaImportSource& source, bool& added) = 0;

  /*
   * \brief Updates the given source in the repository.
   *
   * \param source Source to be updated
   * \param updated[out] Whether the import has been updated in the repository or not.
   * \return True if the source was successfully updated, false otherwise
   */
  virtual bool UpdateSource(const CMediaImportSource& source, bool& updated) = 0;

  /*
   * \brief Removes the source with the given identifier from the repository.
   *
   * \param identifier Source identifier
   * \return True if the source was successfully removed, false otherwise
   */
  virtual bool RemoveSource(const std::string& identifier) = 0;

protected:
  IMediaImportRepository() = default;
};

using MediaImportRepositoryPtr = std::shared_ptr<IMediaImportRepository>;
