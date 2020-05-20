/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportDirectory.h"

#include "DatabaseManager.h"
#include "DbUrl.h"
#include "FileItem.h"
#include "ServiceBroker.h"
#include "filesystem/Directory.h"
#include "guilib/LocalizeStrings.h"
#include "media/MediaType.h"
#include "media/import/IMediaImportRepository.h"
#include "media/import/MediaImportManager.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "video/VideoDbUrl.h"

using namespace std;
using namespace XFILE;

CMediaImportDirectory::CMediaImportDirectory(void)
{
}

CMediaImportDirectory::~CMediaImportDirectory(void)
{
}

bool CMediaImportDirectory::GetDirectory(const CURL& url, CFileItemList& items)
{
  const std::string& strPath = url.Get();
  const auto& hostname = url.GetHostName();
  if (hostname.empty())
  {
    // All
    if (CServiceBroker::GetMediaImportManager().HasSources())
    {
      {
        CFileItemPtr item(new CFileItem(URIUtils::AddFileToFolder(strPath, "all"), true));
        item->SetLabel(g_localizeStrings.Get(39573));
        item->SetSpecialSort(SortSpecialOnTop);
        items.Add(item);
      }

      // Active
      if (CServiceBroker::GetMediaImportManager().HasSources(true))
      {
        CFileItemPtr item(new CFileItem(URIUtils::AddFileToFolder(strPath, "active"), true));
        item->SetLabel(g_localizeStrings.Get(39574));
        items.Add(item);
      }

      // Inactive
      if (CServiceBroker::GetMediaImportManager().HasSources(false))
      {
        CFileItemPtr item(new CFileItem(URIUtils::AddFileToFolder(strPath, "inactive"), true));
        item->SetLabel(g_localizeStrings.Get(39575));
        items.Add(item);
      }
    }

    items.SetLabel(g_localizeStrings.Get(39600));
    return true;
  }
  else
  {
    std::string sourceID;
    if (hostname == "all" || hostname == "active" || hostname == "inactive")
    {
      std::string filename = url.GetFileName();
      URIUtils::RemoveSlashAtEnd(filename);
      if (filename.empty())
      {
        std::vector<CMediaImportSource> sources;
        if (hostname == "all")
        {
          items.SetLabel(g_localizeStrings.Get(39573));
          sources = CServiceBroker::GetMediaImportManager().GetSources();
        }
        else
        {
          bool activeOnly = hostname == "active";
          if (activeOnly)
            items.SetLabel(g_localizeStrings.Get(39574));
          else
            items.SetLabel(g_localizeStrings.Get(39575));

          sources = CServiceBroker::GetMediaImportManager().GetSources(activeOnly);
        }

        HandleSources(strPath, sources, items);
        return true;
      }
      else
        sourceID = filename;
    }
    else
      sourceID = hostname;

    if (sourceID.empty())
      return false;

    URIUtils::RemoveSlashAtEnd(sourceID);
    if (sourceID.find('/') != std::string::npos)
      return false;

    sourceID = CURL::Decode(sourceID);
    CMediaImportSource source(sourceID);
    if (!CServiceBroker::GetMediaImportManager().GetSource(sourceID, source))
      return false;

    items.SetLabel(source.GetFriendlyName());
    const auto imports = CServiceBroker::GetMediaImportManager().GetImportsBySource(sourceID);
    HandleImports(strPath, imports, items);
    return true;
  }

  return false;
}

void CMediaImportDirectory::HandleSources(const std::string& strPath,
                                          const std::vector<CMediaImportSource>& sources,
                                          CFileItemList& items)
{
  for (std::vector<CMediaImportSource>::const_iterator itSource = sources.begin();
       itSource != sources.end(); ++itSource)
  {
    CFileItemPtr item = FileItemFromMediaImportSource(*itSource, strPath);
    if (item != NULL)
      items.Add(item);
  }

  items.SetContent("sources");
}

CFileItemPtr CMediaImportDirectory::FileItemFromMediaImportSource(const CMediaImportSource& source,
                                                                  const std::string& basePath)
{
  if (source.GetIdentifier().empty() || source.GetFriendlyName().empty())
    return CFileItemPtr();

  // prepare the path
  std::string path = basePath + CURL::Encode(source.GetIdentifier());
  URIUtils::AddSlashAtEnd(path);

  CFileItemPtr item(new CFileItem(path, true));
  item->SetLabel(source.GetFriendlyName());
  item->m_dateTime = source.GetLastSynced();

  if (!source.GetIconUrl().empty())
    item->SetArt("thumb", source.GetIconUrl());

  item->SetProperty(PROPERTY_SOURCE_IDENTIFIER, source.GetIdentifier());
  item->SetProperty(PROPERTY_SOURCE_NAME, source.GetFriendlyName());
  item->SetProperty(PROPERTY_SOURCE_BASEPATH, source.GetBasePath());
  item->SetProperty(PROPERTY_SOURCE_ISACTIVE, source.IsActive());
  item->SetProperty(PROPERTY_SOURCE_ISACTIVE_LABEL, source.IsActive()
                                                        ? g_localizeStrings.Get(39576)
                                                        : g_localizeStrings.Get(39577));
  item->SetProperty(PROPERTY_SOURCE_ISREADY, source.IsReady());

  return item;
}

void CMediaImportDirectory::HandleImports(const std::string& strPath,
                                          const std::vector<CMediaImport>& imports,
                                          CFileItemList& items,
                                          bool bySource /* = false */)
{
  for (std::vector<CMediaImport>::const_iterator itImport = imports.begin();
       itImport != imports.end(); ++itImport)
  {
    CFileItemPtr item = FileItemFromMediaImport(*itImport, strPath, bySource);
    if (item != NULL)
      items.Add(item);
  }

  items.SetContent("imports");
}

CFileItemPtr CMediaImportDirectory::FileItemFromMediaImport(const CMediaImport& import,
                                                            const std::string& basePath,
                                                            bool bySource /* = false */)
{
  if (import.GetPath().empty() || import.GetMediaTypes().empty())
    return CFileItemPtr();

  const CMediaImportSource& source = import.GetSource();

  CURL url(URIUtils::AddFileToFolder(basePath, CURL::Encode(import.GetPath())));
  std::string mediaTypes = CMediaTypes::Join(import.GetMediaTypes());
  url.SetOption("mediatypes", mediaTypes);
  std::string path = url.Get();
  std::string mediaTypesLabel = CMediaTypes::ToLabel(import.GetMediaTypes());
  std::string label = mediaTypesLabel;
  if (!bySource)
    label = StringUtils::Format(g_localizeStrings.Get(39565).c_str(),
                                source.GetFriendlyName().c_str(), label.c_str());

  CFileItemPtr item(new CFileItem(path, false));
  item->SetLabel(label);
  item->m_dateTime = import.GetLastSynced();

  if (!source.GetIconUrl().empty())
    item->SetArt("thumb", source.GetIconUrl());

  item->SetProperty(PROPERTY_IMPORT_PATH, import.GetPath());
  item->SetProperty(PROPERTY_IMPORT_MEDIATYPES, mediaTypes);
  item->SetProperty(PROPERTY_IMPORT_NAME,
                    StringUtils::Format(g_localizeStrings.Get(39565).c_str(),
                                        source.GetFriendlyName().c_str(), mediaTypesLabel.c_str()));
  item->SetProperty(PROPERTY_SOURCE_IDENTIFIER, source.GetIdentifier());
  item->SetProperty(PROPERTY_SOURCE_NAME, source.GetFriendlyName());
  item->SetProperty(PROPERTY_SOURCE_BASEPATH, source.GetBasePath());
  item->SetProperty(PROPERTY_SOURCE_ISACTIVE, source.IsActive());
  item->SetProperty(PROPERTY_SOURCE_ISACTIVE_LABEL, source.IsActive()
                                                        ? g_localizeStrings.Get(39576)
                                                        : g_localizeStrings.Get(39577));
  item->SetProperty(PROPERTY_SOURCE_ISREADY, source.IsReady());

  return item;
}
