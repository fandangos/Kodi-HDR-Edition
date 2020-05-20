/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "FileItem.h"
#include "filesystem/IDirectory.h"
#include "media/MediaType.h"

#include <string>
#include <vector>

const std::string PROPERTY_SOURCE_IDENTIFIER = "Source.ID";
const std::string PROPERTY_SOURCE_NAME = "Source.Name";
const std::string PROPERTY_SOURCE_PROTOCOL = "Source.Protocol";
const std::string PROPERTY_SOURCE_BASEPATH = "Source.BasePath";
const std::string PROPERTY_SOURCE_MEDIATYPES_LABEL = "Source.MediaTypesLabel";
const std::string PROPERTY_SOURCE_ISACTIVE = "Source.Active";
const std::string PROPERTY_SOURCE_ISACTIVE_LABEL = "Source.ActiveLabel";
const std::string PROPERTY_SOURCE_ISREADY = "Source.Ready";
const std::string PROPERTY_IMPORT_PATH = "Import.Path";
const std::string PROPERTY_IMPORT_NAME = "Import.Name";
const std::string PROPERTY_IMPORT_MEDIATYPES = "Import.MediaTypes";

class CMediaImport;
class CMediaImportSource;
class IMediaImportRepository;

namespace XFILE
{
class CMediaImportDirectory : public IDirectory
{
public:
  CMediaImportDirectory();
  virtual ~CMediaImportDirectory();

  virtual bool GetDirectory(const CURL& url, CFileItemList& items);
  virtual bool Create(const CURL& url) { return true; }
  virtual bool Exists(const CURL& url) { return true; }
  virtual bool IsAllowed(const CURL& url) const { return true; };

  static CFileItemPtr FileItemFromMediaImportSource(const CMediaImportSource& source,
                                                    const std::string& basePath);
  static CFileItemPtr FileItemFromMediaImport(const CMediaImport& import,
                                              const std::string& basePath,
                                              bool bySource = false);

private:
  static void HandleSources(const std::string& strPath,
                            const std::vector<CMediaImportSource>& sources,
                            CFileItemList& items);

  static void HandleImports(const std::string& strPath,
                            const std::vector<CMediaImport>& imports,
                            CFileItemList& items,
                            bool bySource = false);
};
} // namespace XFILE
