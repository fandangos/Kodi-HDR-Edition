/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/IMediaImporter.h"

#include <memory>
#include <string>

class CMediaImportSource;

/*!
 * \brief Interface defining the functionality of a media importer manager.
 */
class IMediaImporterManager
{
public:
  virtual ~IMediaImporterManager() = default;

  /*!
   * \brief Checks whether an importer with the given identification is available.
   */
  virtual bool HasImporter(const std::string& id) const = 0;

  /*!
   * \brief Returns the media importer implementation with the given identification.
   *
   * \param id Identification of the media importer implementation
   * \return Media importer implementation
   */
  virtual MediaImporterPtr GetImporterById(const std::string& id) const = 0;

  /*!
   * \brief Returns the media importer implementation capable of importing
   * media items from the given source.
   *
   * \param source Source to import items from
   * \return Media importer implementation
   */
  virtual MediaImporterPtr GetImporterBySource(const CMediaImportSource& source) const = 0;

protected:
  IMediaImporterManager() = default;
};
