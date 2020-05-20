/*
 *  Copyright (C) 2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Observer.h"

#include "interfaces/legacy/AddonUtils.h"
#include "utils/log.h"

namespace XBMCAddon
{
namespace xbmcmediaimport
{
Observer::Observer()
{
  XBMC_TRACE;

  // now that we're done register me in the system
  if (languageHook)
  {
    m_importerId = languageHook->GetAddonId();
    if (m_importerId.empty())
      throw ObserverException("Invalid importer identification");
    languageHook->RegisterMediaImporterObserverCallback(m_importerId, this);
  }
}

Observer::~Observer()
{
  XBMC_TRACE;

  deallocating();

  // we're shutting down so unregister me
  if (languageHook)
  {
    DelayedCallGuard dc(languageHook);
    languageHook->UnregisterMediaImporterObserverCallback(m_importerId, this);
  }
}

void Observer::OnSourceAdded(const CMediaImportSource& source)
{
  XBMC_TRACE;
  invokeCallback(new CallbackFunction<Observer, MediaProvider*>(
      this, &Observer::onProviderAdded, new MediaProvider(source, m_importerId)));
}

void Observer::OnSourceUpdated(const CMediaImportSource& source)
{
  XBMC_TRACE;
  invokeCallback(new CallbackFunction<Observer, MediaProvider*>(
      this, &Observer::onProviderUpdated, new MediaProvider(source, m_importerId)));
}

void Observer::OnSourceRemoved(const CMediaImportSource& source)
{
  XBMC_TRACE;
  invokeCallback(new CallbackFunction<Observer, MediaProvider*>(
      this, &Observer::onProviderRemoved, new MediaProvider(source, m_importerId)));
}

void Observer::OnSourceActivated(const CMediaImportSource& source)
{
  XBMC_TRACE;
  invokeCallback(new CallbackFunction<Observer, MediaProvider*>(
      this, &Observer::onProviderActivated, new MediaProvider(source, m_importerId)));
}

void Observer::OnSourceDeactivated(const CMediaImportSource& source)
{
  XBMC_TRACE;
  invokeCallback(new CallbackFunction<Observer, MediaProvider*>(
      this, &Observer::onProviderDeactivated, new MediaProvider(source, m_importerId)));
}

void Observer::OnImportAdded(const CMediaImport& import)
{
  XBMC_TRACE;
  invokeCallback(new CallbackFunction<Observer, MediaImport*>(
      this, &Observer::onImportAdded, new MediaImport(import, m_importerId)));
}

void Observer::OnImportUpdated(const CMediaImport& import)
{
  XBMC_TRACE;
  invokeCallback(new CallbackFunction<Observer, MediaImport*>(
      this, &Observer::onImportUpdated, new MediaImport(import, m_importerId)));
}

void Observer::OnImportRemoved(const CMediaImport& import)
{
  XBMC_TRACE;
  invokeCallback(new CallbackFunction<Observer, MediaImport*>(
      this, &Observer::onImportRemoved, new MediaImport(import, m_importerId)));
}

void Observer::onProviderAdded(MediaProvider* provider)
{
  XBMC_TRACE;
}

void Observer::onProviderUpdated(MediaProvider* provider)
{
  XBMC_TRACE;
}

void Observer::onProviderRemoved(MediaProvider* provider)
{
  XBMC_TRACE;
}

void Observer::onProviderActivated(MediaProvider* provider)
{
  XBMC_TRACE;
}

void Observer::onProviderDeactivated(MediaProvider* provider)
{
  XBMC_TRACE;
}

void Observer::onImportAdded(MediaImport* import)
{
  XBMC_TRACE;
}

void Observer::onImportUpdated(MediaImport* import)
{
  XBMC_TRACE;
}

void Observer::onImportRemoved(MediaImport* import)
{
  XBMC_TRACE;
}
} // namespace xbmcmediaimport
} // namespace XBMCAddon
