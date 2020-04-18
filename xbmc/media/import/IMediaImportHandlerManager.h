/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/MediaType.h"

#include <memory>

class IMediaImportHandler;
using MediaImportHandlerConstPtr = std::shared_ptr<const IMediaImportHandler>;

/*!
 * \brief Interface defining the functionality of a media import handler manager.
 */
class IMediaImportHandlerManager
{
public:
  virtual ~IMediaImportHandlerManager() = default;

  /*!
   * \brief Returns the media import handler implementation capable of handling
   * imported media items of the given media type.
   *
   * \param mediaType Media type of an imported item
   * \pram Media import handler implementation
   */
  virtual MediaImportHandlerConstPtr GetImportHandler(const MediaType& mediaType) const = 0;

protected:
  IMediaImportHandlerManager() = default;
};
