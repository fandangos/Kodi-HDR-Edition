/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "media/import/IMediaImporter.h"
#include "settings/dialogs/GUIDialogSettingsManagerBase.h"
#include "utils/logtypes.h"
#include "view/GUIViewControl.h"

#include <memory>

class CFileItemList;
class CMediaImport;
class CMediaImportSource;

class CGUIDialogMediaImportInfo : public CGUIDialogSettingsManagerBase
{
public:
  CGUIDialogMediaImportInfo();
  virtual ~CGUIDialogMediaImportInfo() = default;

  // specialization of CGUIControl
  bool OnMessage(CGUIMessage& message) override;
  bool OnAction(const CAction& action) override;
  bool OnBack(int actionID) override;

  static bool ShowForMediaImport(const CFileItemPtr& item);
  static bool ShowForMediaImportSource(const CFileItemPtr& item);

protected:
  // specialization of CGUIDialogSettingsBase
  bool AllowResettingSettings() const override { return false; }
  std::string GetLocalizedString(uint32_t labelId) const override;
  void OnCancel() override;
  void SetupView() override;

  // implementation of CGUIDialogSettingsManagerBase
  std::shared_ptr<CSettingSection> GetSection() override;

  // implementation of CGUIDialogSettingsManagerBase
  void Save() override;
  CSettingsManager* GetSettingsManager() const override;

  void InitializeSettings();
  void InitializeMediaImportSettings();
  void InitializeMediaImportSourceSettings();

  bool SetMediaImport(const CFileItemPtr& item);
  bool SetMediaImportSource(const CFileItemPtr& item);

  CFileItemPtr m_item;
  std::shared_ptr<CMediaImport> m_import;
  std::shared_ptr<CMediaImportSource> m_source;
  MediaImporterPtr m_importer;

  Logger m_logger;
};
