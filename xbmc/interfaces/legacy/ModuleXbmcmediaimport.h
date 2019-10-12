/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AddonString.h"
#include "ListItem.h"
#include "swighelper.h"
#include "Tuple.h"
#include "mediaimport/MediaImport.h"
#include "mediaimport/MediaProvider.h"

#include "media/MediaType.h"
#include "media/import/MediaImportChangesetTypes.h"

#include <set>

#ifndef DOXYGEN_SHOULD_SKIP_THIS
namespace XBMCAddon
{
  namespace xbmcmediaimport
  {
    XBMCCOMMONS_STANDARD_EXCEPTION(MediaImportException);

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

    //
    /// \defgroup python_xbmcmediaimport Library - xbmcmediaimport
    /// @{
    /// @brief <b>Media import functions on Kodi.</b>
    ///
    /// Offers classes and functions that allow a developer to import media
    /// items from external sources into Kodi's media library.
    //

#ifdef DOXYGEN_SHOULD_USE_THIS
    ///
    /// \ingroup python_xbmcmediaimport
    /// @brief \python_func{ xbmcmediaimport.addProvider(provider) }
    ///-------------------------------------------------------------------------
    /// Add a (new) media provider from which media imports can be imported.
    ///
    /// @param provider             MediaProvider - Media provider to add.
    /// @return                     Returns a bool for successful completion.
    ///
    /// ------------------------------------------------------------------------
    ///
    /// **Example:**
    /// ~~~~~~~~~~~~~{.py}
    /// ..
    /// xbmcmediaimport.addProvider(provider)
    /// ..
    /// ~~~~~~~~~~~~~
    ///
    addProvider(...);
#else
    bool addProvider(const MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    ///
    /// \ingroup python_xbmcmediaimport
    /// @brief \python_func{ xbmcmediaimport.addAndActivateProvider(provider) }
    ///-------------------------------------------------------------------------
    /// Add a (new) a media provider from which media imports can be imported
    /// and activate it.
    ///
    /// @param provider             MediaProvider - Media provider to add and activate.
    /// @return                     Returns a bool for successful completion.
    ///
    /// ------------------------------------------------------------------------
    ///
    /// **Example:**
    /// ~~~~~~~~~~~~~{.py}
    /// ..
    /// xbmcmediaimport.addAndActivateProvider(provider)
    /// ..
    /// ~~~~~~~~~~~~~
    ///
    addAndActivateProvider(...);
#else
    bool addAndActivateProvider(const MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    ///
    /// \ingroup python_xbmcmediaimport
    /// @brief \python_func{ xbmcmediaimport.activateProvider(provider) }
    ///-------------------------------------------------------------------------
    /// Activate a media provider from which media imports can be imported.
    ///
    /// @param provider             MediaProvider - Media provider to activate.
    /// @return                     Returns a bool for successful completion.
    ///
    /// ------------------------------------------------------------------------
    ///
    /// **Example:**
    /// ~~~~~~~~~~~~~{.py}
    /// ..
    /// xbmcmediaimport.activateProvider(provider)
    /// ..
    /// ~~~~~~~~~~~~~
    ///
    activateProvider(...);
#else
    bool activateProvider(const MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    ///
    /// \ingroup python_xbmcmediaimport
    /// @brief \python_func{ xbmcmediaimport.deactivateProvider(providerId) }
    ///-------------------------------------------------------------------------
    /// Deactivate a media provider.
    ///
    /// @param providerId           string - Unique identifier for the
    ///                             media provider.
    ///
    /// @note You can use the above as keywords for arguments and skip certain
    ///       optional arguments. Once you use a keyword, all following arguments
    ///       require the keyword.
    ///
    ///
    /// ------------------------------------------------------------------------
    ///
    /// **Example:**
    /// ~~~~~~~~~~~~~{.py}
    /// ..
    /// xbmcmediaimport.deactivateProvider("kodi")
    /// ..
    /// ~~~~~~~~~~~~~
    ///
    deactivateProvider(...);
#else
    void deactivateProvider(const String& providerId);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    ///
    /// \ingroup python_xbmcmediaimport
    /// @brief \python_func{ xbmcmediaimport.updateProvider(provider) }
    ///-------------------------------------------------------------------------
    /// Updates the properties of a media provider.
    ///
    /// @param provider             MediaProvider - Media provider to update.
    /// @return                     Returns a bool for successful completion.
    ///
    /// ------------------------------------------------------------------------
    ///
    /// **Example:**
    /// ~~~~~~~~~~~~~{.py}
    /// ..
    /// xbmcmediaimport.updateProvider(provider)
    /// ..
    /// ~~~~~~~~~~~~~
    ///
    updateProvider(...);
#else
    bool updateProvider(const MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setDiscoveredProvider(int handle, bool providerDiscovered, const MediaProvider* mediaProvider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setProviderFound(int handle, bool providerFound);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    ///
    /// \ingroup python_xbmcmediaimport
    /// @brief \python_func{ xbmcmediaimport.setCanImport(handle, canImport) }
    ///-------------------------------------------------------------------------
    /// TODO
    ///
    /// @param handle               int - handle provided by the invocation
    ///                             of the "canimport" action.
    /// @param canImport            bool - Whether or not the import for the
    ///                             given handle can be imported or not.
    ///
    /// ------------------------------------------------------------------------
    ///
    /// **Example:**
    /// ~~~~~~~~~~~~~{.py}
    /// ..
    /// xbmcmediaimport.setCanImport(handle, True)
    /// ..
    /// ~~~~~~~~~~~~~
    ///
    setCanImport(...);
#else
    void setCanImport(int handle, bool canImport);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setProviderReady(int handle, bool providerReady);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setImportReady(int handle, bool importReady);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setCanUpdateMetadataOnProvider(int handle, bool canUpdateMetadataOnProvider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setCanUpdatePlaycountOnProvider(int handle, bool canUpdatePlaycountOnProvider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setCanUpdateLastPlayedOnProvider(int handle, bool canUpdateLastPlayedOnProvider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setCanUpdateResumePositionOnProvider(int handle, bool canUpdateResumePositionOnProvider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    bool shouldCancel(int handle, unsigned int progress, unsigned int total);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void setProgressStatus(int handle, const String& status);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    MediaProvider* getProvider(int handle) throw(MediaImportException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    MediaImport* getImport(int handle) throw(MediaImportException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    std::vector<XBMCAddon::xbmcgui::ListItem*> getImportedItems(int handle, const String& mediaType);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void addImportItem(int handle, const XBMCAddon::xbmcgui::ListItem* item, const String& mediaType,
      int changesetType = 0 /* MediaImportChangesetTypeNone */);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void addImportItems(int handle, const std::vector<XBMCAddon::xbmcgui::ListItem*>& items, const String& mediaType,
      int changesetType = 0 /* MediaImportChangesetTypeNone */);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void finishImport(int handle, bool isChangeset = false);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    XBMCAddon::xbmcgui::ListItem* getUpdatedItem(int handle) throw(MediaImportException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    void finishUpdateOnProvider(int handle);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    typedef Tuple<int, XBMCAddon::xbmcgui::ListItem*> ChangesetListItem;

    bool changeImportedItems(const MediaImport* import, const std::vector<ChangesetListItem>& changedItems);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    MediaProvider* getProviderById(const String& providerId);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    std::vector<XBMCAddon::xbmcgui::ListItem*> getImportedItemsByProvider(const MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
    // TODO(Montellese)
#else
    std::vector<XBMCAddon::xbmcgui::ListItem*> getImportedItemsByImport(const MediaImport* import);
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS
    // media types
    SWIG_CONSTANT(String, MediaTypeNone);
    SWIG_CONSTANT(String, MediaTypeMovie);
    SWIG_CONSTANT(String, MediaTypeVideoCollection);
    SWIG_CONSTANT(String, MediaTypeMusicVideo);
    SWIG_CONSTANT(String, MediaTypeTvShow);
    SWIG_CONSTANT(String, MediaTypeSeason);
    SWIG_CONSTANT(String, MediaTypeEpisode);

    // changeset types
    SWIG_CONSTANT(int, MediaImportChangesetTypeNone);
    SWIG_CONSTANT(int, MediaImportChangesetTypeAdded);
    SWIG_CONSTANT(int, MediaImportChangesetTypeChanged);
    SWIG_CONSTANT(int, MediaImportChangesetTypeRemoved);
  }
}
#endif /* DOXYGEN_SHOULD_SKIP_THIS */
