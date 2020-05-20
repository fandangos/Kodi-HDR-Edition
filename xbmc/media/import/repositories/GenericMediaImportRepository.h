/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/IMediaImportRepository.h"
#include "threads/CriticalSection.h"

#include <map>
#include <set>

class CGenericMediaImportRepository : public IMediaImportRepository
{
public:
  CGenericMediaImportRepository() : m_loaded(false) {}
  virtual ~CGenericMediaImportRepository();

  bool Initialize() override;

  std::vector<CMediaImport> GetImports() const override;
  std::vector<CMediaImport> GetImportsBySource(const std::string& sourceIdentifier) const override;
  std::vector<CMediaImport> GetImportsByMediaType(
      const GroupedMediaTypes& mediaTypes) const override;
  std::vector<CMediaImport> GetImportsByPath(const std::string& path,
                                             bool includeSubDirectories = false) const override;
  bool GetImport(const std::string& path,
                 const GroupedMediaTypes& mediaTypes,
                 CMediaImport& import) const override;

  bool AddImport(const CMediaImport& import, bool& added) override;
  bool UpdateImport(const CMediaImport& import, bool& updated) override;
  bool RemoveImport(const CMediaImport& import) override;

  bool UpdateLastSync(CMediaImport& import) override;

  std::vector<CMediaImportSource> GetSources(
      const GroupedMediaTypes& mediaTypes = GroupedMediaTypes()) const override;
  bool GetSource(const std::string& identifier, CMediaImportSource& source) const override;

  bool AddSource(const CMediaImportSource& source, bool& added) override;
  bool UpdateSource(const CMediaImportSource& source, bool& updated) override;
  bool RemoveSource(const std::string& identifier) override;

protected:
  virtual MediaTypes getSupportedMediaTypes() const = 0;

  virtual bool openRepository() = 0;
  virtual void closeRepository() = 0;

  virtual std::vector<CMediaImportSource> getSources() const = 0;
  virtual int addSource(const CMediaImportSource& source) = 0;
  virtual bool updateSource(const CMediaImportSource& source) = 0;
  virtual void removeSource(const CMediaImportSource& source) = 0;
  virtual std::vector<CMediaImport> getImports() const = 0;
  virtual int addImport(const CMediaImport& import) = 0;
  virtual bool updateImport(const CMediaImport& import) = 0;
  virtual void removeImport(const CMediaImport& import) = 0;
  virtual void updateLastSync(const CMediaImport& import, const CDateTime& lastSync) = 0;

  typedef std::pair<std::string, GroupedMediaTypes> MediaImportIdentifier;
  static MediaImportIdentifier GetMediaImportIdentifier(const CMediaImport& import);

  static bool ContainsAllMediaTypes(const MediaTypes& mediaTypes,
                                    const GroupedMediaTypes& groupedMediaTypes);

  bool m_loaded;

  typedef std::map<MediaImportIdentifier, CMediaImport> MediaImportMap;
  mutable CCriticalSection m_importsLock;
  MediaImportMap m_imports;

  typedef std::map<std::string, CMediaImportSource> MediaImportSourceMap;
  mutable CCriticalSection m_sourcesLock;
  MediaImportSourceMap m_sources;
};
