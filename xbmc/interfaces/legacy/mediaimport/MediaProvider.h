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
#include "media/import/MediaImportSource.h"
#include "media/import/importers/AddonMediaImporter.h"

#include <set>
#include <string>

#ifndef SWIG
class CAddonMediaImporter;
#endif

namespace XBMCAddon
{
  namespace xbmcmediaimport
  {
    class MediaImport;

    typedef std::set<String> MediaTypes;

    //
    /// \defgroup python_xbmcmediaimport_mediaimport MediaProvider
    /// \ingroup python_xbmcmediaimport
    /// @{
    /// @brief **Media provider capable of importing media items from**
    ///
    /// A media provider represents a source from which media items can be imported.
    ///
    /// \python_class{ MediaProvider(identifier [, basePath, friendlyName, iconUrl, availableMediaTypes, lastSynced, settingValues]) }
    ///
    /// @param identifier           string
    /// @param basePath             [opt] string
    /// @param friendlyName         [opt] string
    /// @param iconUrl              [opt] string
    /// @param availableMediaTypes  [opt] MediaTypes
    /// @param lastSynced           [opt] string
    ///
    ///
    ///-----------------------------------------------------------------------
    /// @python_v19
    ///
    /// **Example:**
    /// ~~~~~~~~~~~~~{.py}
    /// ...
    /// mediaProvider = xbmcmediaimport.MediaProvider('kodi')
    /// ...
    /// ~~~~~~~~~~~~~
    class MediaProvider : public AddonClass
    {
    public:
#if !defined SWIG && !defined DOXYGEN_SHOULD_SKIP_THIS
      MediaImportSourcePtr source;
#endif

#ifndef SWIG
      explicit MediaProvider(const std::string& addonId = "",
        CAddonMediaImporter* addonMediaImporter = nullptr);
      explicit MediaProvider(MediaImportSourcePtr source, const std::string& addonId = "",
        CAddonMediaImporter* addonMediaImporter = nullptr);
      explicit MediaProvider(const CMediaImportSource& source, const std::string& addonId = "",
        CAddonMediaImporter* addonMediaImporter = nullptr);
#endif

      MediaProvider(const String& identifier, const String& basePath = emptyString,
        const String& friendlyName = emptyString, const String& iconUrl = emptyString,
        const MediaTypes& mediaTypes = {}, const String& lastSynced = emptyString,
        int handle = -1);

      virtual ~MediaProvider() { }

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ getIdentifier() }
      ///-----------------------------------------------------------------------
      /// Returns the media provider's identifier.
      ///
      /// @return  Identifier of the media provider
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # geIdentifier()
      /// identifier = mediaProvider.getIdentifier()
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      getIdentifier();
#else
      String getIdentifier() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ getBasePath() }
      ///-----------------------------------------------------------------------
      /// Returns the media provider's base path.
      ///
      /// @return  Base path of the media provider
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # getBasePath()
      /// basePath = mediaProvider.getBasePath()
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      getBasePath();
#else
      String getBasePath() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ setBasePath(basePath) }
      ///-----------------------------------------------------------------------
      /// Sets the media provider's base path.
      ///
      /// @param basePath  string or unicode - base path of the media provider.
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # setBasePath(basePath)
      /// mediaProvider.setBasePath('http://localhost/')
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      setBasePath(...);
#else
      void setBasePath(const String& basePath);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ getFriendlyName() }
      ///-----------------------------------------------------------------------
      /// Returns the media provider's human readable name.
      ///
      /// @return  Human readable name of the media provider
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # getFriendlyName()
      /// name = mediaProvider.getFriendlyName()
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      getFriendlyName();
#else
      String getFriendlyName() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ setFriendlyName(friendlyName) }
      ///-----------------------------------------------------------------------
      /// Sets the media provider's human readable name.
      ///
      /// @param friendlyName  string or unicode - human readable name of the media provider.
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # setFriendlyName(friendlyName)
      /// mediaProvider.setFriendlyName('Kodi')
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      setFriendlyName(...);
#else
      void setFriendlyName(const String& friendlyName);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ getIconUrl() }
      ///-----------------------------------------------------------------------
      /// Returns the media provider's icon URL.
      ///
      /// @return Icon URL of the media provider
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # getIconUrl()
      /// iconUrl = mediaProvider.getIconUrl()
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      getIconUrl();
#else
      String getIconUrl() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ setIconUrl(iconUrl) }
      ///-----------------------------------------------------------------------
      /// Sets the media provider's icon URL.
      ///
      /// @param iconUrl  string or unicode - icon URL of the media provider.
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # setIconUrl(iconUrl)
      /// mediaProvider.setIconUrl('http://kodi/icon.png')
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      setIconUrl(...);
#else
      void setIconUrl(const String& iconUrl);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ getAvailableMediaTypes() }
      ///-----------------------------------------------------------------------
      /// Returns the media provider's available media types.
      ///
      /// @return Media types available from the media provider
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # getAvailableMediaTypes()
      /// mediaTypes = mediaProvider.getAvailableMediaTypes()
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      getAvailableMediaTypes();
#else
      MediaTypes getAvailableMediaTypes() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ setAvailableMediaTypes(mediaTypes) }
      ///-----------------------------------------------------------------------
      /// Sets the media provider's icon URL.
      ///
      /// @param mediaTypes  set - set of media types available from the media provider.
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # setAvailableMediaTypes(mediaTypes)
      /// mediaProvider.setAvailableMediaTypes('http://kodi/icon.png')
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      setAvailableMediaTypes(...);
#else
      void setAvailableMediaTypes(const MediaTypes& mediaTypes);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ getLastSynced() }
      ///-----------------------------------------------------------------------
      /// Returns when the media provider was last synchronized.
      ///
      /// @return Time when the media provider was last synchronized
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # getLastSynced()
      /// lastSynced = mediaProvider.getLastSynced()
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
      /// Prepares and returns the settings of the media provider.
      ///
      /// @return Settings of the media provider
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # prepareSettings()
      /// settings = mediaProvider.prepareSettings()
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      prepareSettings();
#else
      XBMCAddon::xbmcaddon::Settings* prepareSettings();
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ getSettings() }
      ///-----------------------------------------------------------------------
      /// Returns the settings of the media provider.
      ///
      /// @return Settings of the media provider
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

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ isActive() }
      ///-----------------------------------------------------------------------
      /// Returns whether the media provider is active or not.
      ///
      /// @return True if the media provider is active otherwise false
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # isActive()
      /// isActive = mediaProvider.isActive()
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      isActive();
#else
      bool isActive() const;
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
      ///
      /// \ingroup python_xbmcmediaimport_mediaimport
      /// @brief \python_func{ getImports() }
      ///-----------------------------------------------------------------------
      /// Returns all media imports belonging to the provider.
      ///
      /// @return List of media imports belonging to the provider.
      ///
      ///
      ///-----------------------------------------------------------------------
      /// @python_v19
      ///
      /// **Example:**
      /// ~~~~~~~~~~~~~{.py}
      /// ...
      /// # getImports()
      /// imports = mediaProvider.getImports()
      /// ...
      /// ~~~~~~~~~~~~~
      ///
      getImports();
#else
      std::vector<MediaImport*> getImports() const;
#endif

#ifndef SWIG
    private:
      XBMCAddon::xbmcaddon::Settings* createSettings(MediaImportSourceSettingsPtr settings);

      std::string m_addonId;
      CAddonMediaImporter* m_addonMediaImporter;
      CAddonMediaImporter::SettingsCallbackData m_callbackData;
#endif
    };
  }
}
