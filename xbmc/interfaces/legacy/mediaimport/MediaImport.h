/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "commons/Exception.h"
#include "interfaces/legacy/AddonClass.h"
#include "interfaces/legacy/AddonString.h"
#include "interfaces/legacy/Settings.h"
#include "media/import/MediaImport.h"
#include "media/import/importers/AddonMediaImporter.h"

#include <set>
#include <string>

namespace XBMCAddon
{
namespace xbmcmediaimport
{
class MediaProvider;

//
/// \defgroup python_xbmcmediaimport_mediapimport MediaImport
/// \ingroup python_xbmcmediaimport
/// @{
/// @brief **Media import**
///
/// A media import contains the necessary information to import media
/// items from a specific path on a media provider.
///
/// \python_class{ MediaImport(importPath) }
///
/// @param importPath           string
///
///
///-----------------------------------------------------------------------
/// @python_v19
///
/// **Example:**
/// ~~~~~~~~~~~~~{.py}
/// ...
/// mediaImport = xbmcmediaimport.MediaImport('kodi')
/// ...
/// ~~~~~~~~~~~~~
class MediaImport : public AddonClass
{
public:
#if !defined SWIG && !defined DOXYGEN_SHOULD_SKIP_THIS
  MediaImportPtr import;
#endif

#ifndef SWIG
  explicit MediaImport(MediaImportPtr import,
                       const std::string& addonId = "",
                       CAddonMediaImporter* addonMediaImporter = nullptr);
  explicit MediaImport(const CMediaImport& import,
                       const std::string& addonId = "",
                       CAddonMediaImporter* addonMediaImporter = nullptr);
#endif

  MediaImport(const String& importPath);

  virtual ~MediaImport() {}

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_mediapimport
  /// @brief \python_func{ getPath() }
  ///-----------------------------------------------------------------------
  /// Returns the media import's path.
  ///
  /// @return  Path of the media import
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ...
  /// # getPath()
  /// importPath = mediaImport.getPath()
  /// ...
  /// ~~~~~~~~~~~~~
  ///
  getPath();
#else
  String getPath() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_mediapimport
  /// @brief \python_func{ getProvider() }
  ///-----------------------------------------------------------------------
  /// Returns the media provider the media import belongs to.
  ///
  /// @return  Media provider the media import belongs to
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ...
  /// # getProvider()
  /// mediaProvider = mediaImport.getProvider()
  /// ...
  /// ~~~~~~~~~~~~~
  ///
  getProvider();
#else
  MediaProvider* getProvider() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_mediapimport
  /// @brief \python_func{ setProvider(provider) }
  ///-----------------------------------------------------------------------
  /// Sets the media provider the media import belongs to.
  ///
  /// @param provider  Media provider the media import belongs to
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ...
  /// # setProvider(provider)
  /// mediaImport.setProvider(mediaProvider)
  /// ...
  /// ~~~~~~~~~~~~~
  ///
  setProvider(...);
#else
  void setProvider(const MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_mediapimport
  /// @brief \python_func{ getMediaTypes() }
  ///-----------------------------------------------------------------------
  /// Returns the media import's media types.
  ///
  /// @return  Media import's media types
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ...
  /// # getMediaTypes()
  /// mediaTypes = mediaImport.getMediaTypes()
  /// ...
  /// ~~~~~~~~~~~~~
  ///
  getMediaTypes();
#else
  std::vector<String> getMediaTypes() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_mediapimport
  /// @brief \python_func{ setMediaTypes(mediaTypes) }
  ///-----------------------------------------------------------------------
  /// Sets the media import's media types
  ///
  /// @param mediaTypes  Media import's media types
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ...
  /// # setMediaTypes(mediaTypes)
  /// mediaImport.setMediaTypes(mediaTypes)
  /// ...
  /// ~~~~~~~~~~~~~
  ///
  setMediaTypes(...);
#else
  void setMediaTypes(const std::vector<String>& mediaTypes);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_mediapimport
  /// @brief \python_func{ getLastSynced() }
  ///-----------------------------------------------------------------------
  /// Returns when the media import was last synchronized.
  ///
  /// @return Time when the media import was last synchronized
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ...
  /// # getLastSynced()
  /// lastSynced = mediaImport.getLastSynced()
  /// ...
  /// ~~~~~~~~~~~~~
  ///
  getLastSynced();
#else
  String getLastSynced() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_mediaimport
  /// @brief \python_func{ prepareSettings() }
  ///-----------------------------------------------------------------------
  /// Prepares and returns the settings of the media import.
  ///
  /// @return Settings of the media import
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ...
  /// # prepareSettings()
  /// settings = mediaImport.prepareSettings()
  /// ...
  /// ~~~~~~~~~~~~~
  ///
  prepareSettings();
#else
  XBMCAddon::xbmcaddon::Settings* prepareSettings();
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_mediapimport
  /// @brief \python_func{ getSettings() }
  ///-----------------------------------------------------------------------
  /// Returns the settings of the media import.
  ///
  /// @return Settings of the media import
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ...
  /// # getSettings()
  /// settings = mediaProvider.getSettings()
  /// ...
  /// ~~~~~~~~~~~~~
  ///
  getSettings();
#else
  XBMCAddon::xbmcaddon::Settings* getSettings();
#endif

#ifndef SWIG
private:
  XBMCAddon::xbmcaddon::Settings* createSettings(MediaImportSourceSettingsPtr settings);

  std::string m_addonId;
  CAddonMediaImporter* m_addonMediaImporter;
  CAddonMediaImporter::SettingsCallbackData m_callbackData;
#endif
};
} // namespace xbmcmediaimport
} // namespace XBMCAddon
