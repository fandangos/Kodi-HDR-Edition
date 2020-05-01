/*
 *  Copyright (C) 2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "interfaces/legacy/AddonCallback.h"
#include "interfaces/legacy/AddonString.h"
#include "interfaces/legacy/Exception.h"
#include "interfaces/legacy/mediaimport/MediaImport.h"
#include "interfaces/legacy/mediaimport/MediaProvider.h"
#include "interfaces/legacy/swighelper.h"
#include "media/import/IMediaImporter.h"

#include <vector>

namespace XBMCAddon
{
namespace xbmcmediaimport
{
XBMCCOMMONS_STANDARD_EXCEPTION(ObserverException);

//
/// \defgroup python_xbmcmediaimport_observer Observer
/// \ingroup python_xbmcmediaimport
/// @{
/// @brief TODO(Montellese)
///
/// \python_class{ xbmcmediaimport.Observer() }
///
/// TODO(Montellese)
///
///
///
///--------------------------------------------------------------------------
///
/// **Example:**
/// ~~~~~~~~~~~~~{.py}
/// ...
/// xbmcmediaimport.Observer()
/// TODO(Montellese)
/// ...
/// ~~~~~~~~~~~~~
//
class Observer : public AddonCallback, public IMediaImporterObserver
{
public:
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  // Construct an Observer proxying the given generated binding. The
  //  construction of an Observer needs to identify whether or not any
  //  callbacks will be executed asynchronously or not.
  Observer();
  ~Observer() override;
#endif

  //
  /// @defgroup python_xbmcmediaimport_observer_ObserverCB Callback functions from Kodi to Add-On
  /// \ingroup python_xbmcmediaimport_observer
  /// @{
  /// @brief **Callback functions.**
  ///
  /// Functions to handle control callbacks from Kodi to Add-On.
  ///
  /// ----------------------------------------------------------------------
  ///
  /// @link python_xbmcmediaimport_observer Go back to normal functions from observer@endlink
  //

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_observer_ObserverCB
  /// @brief \python_func{ onProviderAdded(provider) }
  ///-----------------------------------------------------------------------
  /// onProviderAdded method.
  ///
  /// Will be called when a new media provider has been added.
  ///
  /// @param provider               [MediaProvider] Media provider which has been added
  ///
  onProviderAdded(MediaProvider* provider);
#else
  virtual void onProviderAdded(MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_observer_ObserverCB
  /// @brief \python_func{ onProviderUpdated(provider) }
  ///-----------------------------------------------------------------------
  /// onProviderUpdated method.
  ///
  /// Will be called when a media provider has been updated.
  ///
  /// @param provider               [MediaProvider] Media provider which has been updated
  ///
  onProviderUpdated(MediaProvider* provider);
#else
  virtual void onProviderUpdated(MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_observer_ObserverCB
  /// @brief \python_func{ onProviderRemoved(provider) }
  ///-----------------------------------------------------------------------
  /// onProviderRemoved method.
  ///
  /// Will be called when a media provider has been removed.
  ///
  /// @param provider               [MediaProvider] Media provider which has been removed
  ///
  onProviderRemoved(MediaProvider* provider);
#else
  virtual void onProviderRemoved(MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_observer_ObserverCB
  /// @brief \python_func{ onProviderActivated(provider) }
  ///-----------------------------------------------------------------------
  /// onProviderActivated method.
  ///
  /// Will be called when a media provider has been activated.
  ///
  /// @param provider               [MediaProvider] Media provider which has been activated
  ///
  onProviderActivated(MediaProvider* provider);
#else
  virtual void onProviderActivated(MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_observer_ObserverCB
  /// @brief \python_func{ onProviderDeactivated(provider) }
  ///-----------------------------------------------------------------------
  /// onProviderDeactivated method.
  ///
  /// Will be called when a media provider has been deactivated.
  ///
  /// @param provider               [MediaProvider] Media provider which has been deactivated
  ///
  onProviderDeactivated(MediaProvider* provider);
#else
  virtual void onProviderDeactivated(MediaProvider* provider);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_observer_ObserverCB
  /// @brief \python_func{ onImportAdded(import) }
  ///-----------------------------------------------------------------------
  /// onImportAdded method.
  ///
  /// Will be called when a new media import has been added.
  ///
  /// @param import               [MediaImport] Media import which has been added
  ///
  onImportAdded(MediaImport* import);
#else
  virtual void onImportAdded(MediaImport* import);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_observer_ObserverCB
  /// @brief \python_func{ onImportUpdated(import) }
  ///-----------------------------------------------------------------------
  /// onImportUpdated method.
  ///
  /// Will be called when a media import has been updated.
  ///
  /// @param import               [MediaImport] Media import which has been updated
  ///
  onImportUpdated(MediaImport* import);
#else
  virtual void onImportUpdated(MediaImport* import);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcmediaimport_observer_ObserverCB
  /// @brief \python_func{ onImportRemoved(import) }
  ///-----------------------------------------------------------------------
  /// onImportRemoved method.
  ///
  /// Will be called when a media import has been removed.
  ///
  /// @param import               [MediaImport] Media import which has been removed
  ///
  onImportRemoved(MediaImport* import);
#else
  virtual void onImportRemoved(MediaImport* import);
#endif

#if !defined SWIG && !defined DOXYGEN_SHOULD_SKIP_THIS
  SWIGHIDDENVIRTUAL void OnSourceAdded(const CMediaImportSource& source) override;
  SWIGHIDDENVIRTUAL void OnSourceUpdated(const CMediaImportSource& source) override;
  SWIGHIDDENVIRTUAL void OnSourceRemoved(const CMediaImportSource& source) override;
  SWIGHIDDENVIRTUAL void OnSourceActivated(const CMediaImportSource& source) override;
  SWIGHIDDENVIRTUAL void OnSourceDeactivated(const CMediaImportSource& source) override;
  SWIGHIDDENVIRTUAL void OnImportAdded(const CMediaImport& import) override;
  SWIGHIDDENVIRTUAL void OnImportUpdated(const CMediaImport& import) override;
  SWIGHIDDENVIRTUAL void OnImportRemoved(const CMediaImport& import) override;
#endif

private:
  String m_importerId;
};
} // namespace xbmcmediaimport
} // namespace XBMCAddon
