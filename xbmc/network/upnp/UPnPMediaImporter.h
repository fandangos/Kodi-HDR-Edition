/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/importers/BaseMediaImporter.h"
#include "utils/logtypes.h"

#include <Platinum/Source/Core/PltDeviceData.h>

class CUPnPMediaImporterBase : public virtual IMediaImporterBase
{
public:
  static constexpr char IDENTIFICATION[] = "UPnPMediaImporter";

  ~CUPnPMediaImporterBase() override = default;

  // implementations of IMediaImporterBase
  const char* GetIdentification() const override { return IDENTIFICATION; }
  bool CanLookupSource() const override { return false; }
  std::string GetSourceLookupProtocol() const override { return "UPnP"; }

protected:
  CUPnPMediaImporterBase() = default;
};

class CUPnPMediaImporterDiscoverer : public IMediaImporterDiscoverer, public CUPnPMediaImporterBase
{
public:
  CUPnPMediaImporterDiscoverer() = default;
  ~CUPnPMediaImporterDiscoverer() override = default;

  // implementations of IMediaImporterDiscoverer
  void Start() override { }
};

class CUPnPMediaImporter : public CUPnPMediaImporterBase, public CBaseMediaImporter
{
public:
  CUPnPMediaImporter();
  ~CUPnPMediaImporter() override = default;

  // implementations of IMediaImporter
  bool DiscoverSource(CMediaImportSource& source) override { return false; }
  bool LookupSource(const CMediaImportSource& source) override { return false; }

  bool CanImport(const std::string &path) override;
  bool IsSourceReady(CMediaImportSource& source) override;
  bool IsImportReady(CMediaImport& import) override;
  bool CanUpdateMetadataOnSource(const std::string &path) override { return false; }
  bool CanUpdatePlaycountOnSource(const std::string &path) override;
  bool CanUpdateLastPlayedOnSource(const std::string &path) override;
  bool CanUpdateResumePositionOnSource(const std::string &path) override;

  bool Import(CMediaImportImportItemsRetrievalTask *task) override;
  bool UpdateOnSource(CMediaImportUpdateTask* task) override;

protected:
  static bool getDeviceIdentifier(const std::string& path, std::string& deviceIdentifier);
  static bool validatePath(const std::string &path, PLT_DeviceDataReference &device);
  static bool isXbmcServer(const std::string &path);

  Logger m_logger;
};

class CUPnPMediaImporterObserver : public IMediaImporterObserver
{
public:
  CUPnPMediaImporterObserver() = default;
  ~CUPnPMediaImporterObserver() override = default;

  // implementations of IMediaImporterObserver
  void OnSourceAdded(const CMediaImportSource& source) override { }
  void OnSourceUpdated(const CMediaImportSource& source) override { }
  void OnSourceRemoved(const CMediaImportSource& source) override { }
  void OnSourceActivated(const CMediaImportSource& source) override { }
  void OnSourceDeactivated(const CMediaImportSource& source) override { }
  void OnImportAdded(const CMediaImport& import) override { }
  void OnImportUpdated(const CMediaImport& import) override { }
  void OnImportRemoved(const CMediaImport& import) override { }
};

class CUPnPMediaImporterFactory : public IMediaImporterFactory
{
public:
  CUPnPMediaImporterFactory() = default;
  ~CUPnPMediaImporterFactory() override = default;

  // implementations of IMediaImporterFactory
  const char* GetIdentification() const override { return CUPnPMediaImporterBase::IDENTIFICATION; }

  std::unique_ptr<IMediaImporterDiscoverer> CreateDiscoverer() const override { return std::make_unique<CUPnPMediaImporterDiscoverer>(); }
  std::unique_ptr<IMediaImporter> CreateImporter() const override { return std::make_unique<CUPnPMediaImporter>(); }
  std::unique_ptr<IMediaImporterObserver> CreateObserver() const override { return std::make_unique<CUPnPMediaImporterObserver>(); }
};
