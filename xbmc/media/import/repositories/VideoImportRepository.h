/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/repositories/GenericMediaImportRepository.h"
#include "threads/CriticalSection.h"
#include "video/VideoDatabase.h"

#include <map>

class CVideoImportRepository : public CGenericMediaImportRepository
{
public:
  CVideoImportRepository() = default;
  virtual ~CVideoImportRepository() = default;

protected:
  MediaTypes getSupportedMediaTypes() const override;

  bool openRepository() override { return m_db.Open(); }
  void closeRepository() override { m_db.Close(); }

  std::vector<CMediaImportSource> getSources() const override;
  int addSource(const CMediaImportSource& source) override;
  bool updateSource(const CMediaImportSource& source) override;
  void removeSource(const CMediaImportSource& source) override;
  std::vector<CMediaImport> getImports() const override;
  int addImport(const CMediaImport& import) override;
  bool updateImport(const CMediaImport& import) override;
  void removeImport(const CMediaImport& import) override;
  void updateLastSync(const CMediaImport& import, const CDateTime& lastSync) override;

  mutable CVideoDatabase m_db;
};
