/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoImportRepository.h"

#include "FileItem.h"
#include "video/VideoInfoTag.h"

MediaTypes CVideoImportRepository::getSupportedMediaTypes() const
{
  MediaTypes supportedMediaTypes;
  supportedMediaTypes.insert(MediaTypeMovie);
  supportedMediaTypes.insert(MediaTypeVideoCollection);
  supportedMediaTypes.insert(MediaTypeTvShow);
  supportedMediaTypes.insert(MediaTypeSeason);
  supportedMediaTypes.insert(MediaTypeEpisode);
  supportedMediaTypes.insert(MediaTypeMusicVideo);
  return supportedMediaTypes;
}

std::vector<CMediaImportSource> CVideoImportRepository::getSources() const
{
  return m_db.GetSources();
}

int CVideoImportRepository::addSource(const CMediaImportSource& source)
{
  return m_db.AddSource(source);
}

bool CVideoImportRepository::updateSource(const CMediaImportSource& source)
{
  return m_db.SetDetailsForSource(source);
}

void CVideoImportRepository::removeSource(const CMediaImportSource& source)
{
  m_db.RemoveSource(source.GetIdentifier());
}

std::vector<CMediaImport> CVideoImportRepository::getImports() const
{
  return m_db.GetImports();
}

int CVideoImportRepository::addImport(const CMediaImport& import)
{
  return m_db.AddImport(import);
}

bool CVideoImportRepository::updateImport(const CMediaImport& import)
{
  return m_db.SetDetailsForImport(import);
}

void CVideoImportRepository::removeImport(const CMediaImport& import)
{
  m_db.RemoveImport(import);
}

void CVideoImportRepository::updateLastSync(const CMediaImport& import, const CDateTime& lastSync)
{
  m_db.UpdateImportLastSynced(import, lastSync);
}
