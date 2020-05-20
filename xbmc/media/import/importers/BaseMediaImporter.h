/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/IMediaImporter.h"

/*!
 * \brief Interface of a media importer capable of importing media items from
 * a specific source into the local library.
 */
class CBaseMediaImporter : public IMediaImporter
{
public:
  virtual ~CBaseMediaImporter() = default;

  // specializations of IMediaImporter
  bool LoadSourceSettings(CMediaImportSource& source) override;
  bool UnloadSourceSettings(CMediaImportSource& source) override;
  bool LoadImportSettings(CMediaImport& import) override;
  bool UnloadImportSettings(CMediaImport& import) override;

protected:
  CBaseMediaImporter() = default;

private:
  bool LoadSettings(MediaImportSettingsBasePtr settings) const;
  bool UnloadSettings(MediaImportSettingsBasePtr settings) const;
};
