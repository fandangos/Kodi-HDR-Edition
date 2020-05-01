/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/MediaType.h"
#include "media/import/IMediaImporter.h"
#include "media/import/MediaImportSource.h"
#include "utils/logtypes.h"
#include "windows/GUIMediaWindow.h"

#include <set>
#include <string>

class CGUIWindowMediaSourceBrowser : public CGUIMediaWindow
{
public:
  CGUIWindowMediaSourceBrowser();
  virtual ~CGUIWindowMediaSourceBrowser() = default;

  // specialization of CGUIControl
  virtual bool OnMessage(CGUIMessage& message) override;

protected:
  // specialization of CGUIMediaWindow
  virtual bool GetDirectory(const std::string& strDirectory, CFileItemList& items) override;
  virtual std::string GetStartFolder(const std::string& dir) override;

  virtual void GetContextButtons(int itemNumber, CContextButtons& buttons) override;
  virtual bool OnContextButton(int itemNumber, CONTEXT_BUTTON button) override;

  virtual bool OnClick(int iItem, const std::string& player = "") override;

  static std::string ShowAndGetImporterToLookup(const std::vector<MediaImporterPtr>& importers);
  static std::string ShowAndGetMediaSourcesToImportFrom(
      const GroupedMediaTypes& mediaTypes = GroupedMediaTypes());
  static GroupedMediaTypes ShowAndGetMediaTypesToImport(const std::string& sourceId);
  static GroupedMediaTypes ShowAndGetMediaTypesToImport(const CMediaImportSource& source);
  static bool GetMediaTypesToImport(const MediaTypes& availableMediaTypes, CFileItemList& items);

  bool RefreshList(bool keepSelectedItem = true);

  bool OnSourceSynchronise(const CFileItemPtr& item);
  bool OnSourceInfo(const CFileItemPtr& item);
  bool OnSourceDelete(const CFileItemPtr& item);
  bool OnImportSynchronise(const CFileItemPtr& item);
  bool OnImportInfo(const CFileItemPtr& item);
  bool OnImportDelete(const CFileItemPtr& item);

  void OnSourceAdded(const std::string& sourceId);
  void OnSourceUpdated(const std::string& sourceId);
  void OnSourceRemoved(const std::string& sourceId);
  void OnImportAdded(const std::string& importPath, const GroupedMediaTypes& mediaTypes);
  void OnImportUpdated(const std::string& importPath, const GroupedMediaTypes& mediaTypes);
  void OnImportRemoved(const std::string& importPath, const GroupedMediaTypes& mediaTypes);
  void OnSourceIsActiveChanged(const std::string& sourceId, bool isactive);

  CFileItemPtr GetImportItem(const std::string& importPath,
                             const GroupedMediaTypes& mediaTypes) const;
  CFileItemPtr GetSourceItem(const std::string& sourceId) const;

  bool AddNewImport(CFileItemPtr newImportItem, const CURL& url);
  bool AddNewSource(CFileItemPtr newImportItem, const CURL& url);

  std::string m_manuallyAddedSourceId;

  Logger m_logger;
};
