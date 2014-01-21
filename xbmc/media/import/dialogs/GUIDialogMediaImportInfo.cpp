/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIDialogMediaImportInfo.h"

#include "FileItem.h"
#include "ServiceBroker.h"
#include "filesystem/MediaImportDirectory.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/WindowIDs.h"
#include "input/Key.h"
#include "media/MediaType.h"
#include "media/import/IMediaImporter.h"
#include "media/import/MediaImportManager.h"
#include "settings/lib/SettingsManager.h"
#include "utils/Variant.h"
#include "utils/log.h"

#include <fmt/ostream.h>

#define CONTROL_LIST_AREA                                     CONTROL_SETTINGS_CUSTOM + 1

#define SETTING_SYNCHRONISATION_IMPORT_TRIGGER                "synchronisation.importtrigger"
#define SETTING_SYNCHRONISATION_UPDATE_ITEMS                  "synchronisation.updateimporteditems"
#define SETTING_SYNCHRONISATION_UPDATE_PLAYBACK_FROM_SOURCE   "synchronisation.updateplaybackmetadatafromsource"
#define SETTING_SYNCHRONISATION_UPDATE_METADATA_ON_SOURCE     "synchronisation.updatemetadataonsource"
#define SETTING_SYNCHRONISATION_UPDATE_PLAYBACK_ON_SOURCE     "synchronisation.updateplaybackmetadataonsource"

CGUIDialogMediaImportInfo::CGUIDialogMediaImportInfo()
  : CGUIDialogSettingsManagerBase(WINDOW_DIALOG_MEDIAIMPORT_INFO, "DialogMediaImportInfo.xml")
  , m_item(std::make_shared<CFileItem>())
  , m_import(nullptr)
  , m_source(nullptr)
  , m_importer(nullptr)
{
  m_loadType = KEEP_IN_MEMORY;
}

bool CGUIDialogMediaImportInfo::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
    case GUI_MSG_WINDOW_DEINIT:
    {
      // get rid of the CMediaImport instance
      m_import.reset();
      // get rid of the CMediaImportSource instance
      m_source.reset();
      // get rid of the IMediaImporter instance
      m_importer.reset();

      break;
    }

    case GUI_MSG_CLICKED:
    {
      if (message.GetSenderId() == CONTROL_SETTINGS_CUSTOM_BUTTON)
      {
        OnResetSettings();
        return true;
      }

      break;
    }

    default:
        break;
  }

  return CGUIDialogSettingsManagerBase::OnMessage(message);
}

bool CGUIDialogMediaImportInfo::OnAction(const CAction& action)
{
  if (action.GetID() == ACTION_SHOW_INFO)
  {
    OnOkay();
    Close();
    return true;
  }

  return CGUIDialogSettingsManagerBase::OnAction(action);
}

bool CGUIDialogMediaImportInfo::OnBack(int actionID)
{
  // handle this the same as cancelling the dialog
  OnCancel();

  return CGUIDialogSettingsManagerBase::OnBack(actionID);
}

bool CGUIDialogMediaImportInfo::ShowForMediaImport(const CFileItemPtr& item)
{
  if (item == nullptr)
    return false;

  auto dialog = static_cast<CGUIDialogMediaImportInfo*>(CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_MEDIAIMPORT_INFO));
  if (!dialog)
    return false;

  // TODO(Montellese): showing this dialog while the responsible importer or the matching import is being synchronized
  //                   might have unexpected side effects

  if (!dialog->SetMediaImport(item))
    return false;

  dialog->Open();
  return dialog->IsConfirmed();
}

bool CGUIDialogMediaImportInfo::ShowForMediaImportSource(const CFileItemPtr& item)
{
  if (item == nullptr)
    return false;

  auto dialog = static_cast<CGUIDialogMediaImportInfo*>(CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_MEDIAIMPORT_INFO));
  if (!dialog)
    return false;

  // TODO(Montellese): showing this dialog while the responsible importer or the matching import is being synchronized
  //                   might have unexpected side effects

  if (!dialog->SetMediaImportSource(item))
    return false;

  dialog->Open();
  return dialog->IsConfirmed();
}

std::string CGUIDialogMediaImportInfo::GetLocalizedString(uint32_t labelId) const
{
  // first try to get the localized string the common way
  auto localizedString = CGUIDialogSettingsManagerBase::GetLocalizedString(labelId);
  if (!localizedString.empty())
    return localizedString;

  // now try to get it from the importer
  if (m_importer == nullptr)
    return "";

  return m_importer->Localize(labelId);
}

void CGUIDialogMediaImportInfo::OnCancel()
{
  if (m_import != nullptr)
    m_importer->UnloadImportSettings(*m_import);
  else if (m_source != nullptr)
    m_importer->UnloadSourceSettings(*m_source);

  CGUIDialogSettingsManagerBase::OnCancel();
}

void CGUIDialogMediaImportInfo::SetupView()
{
  InitializeSettings();

  CGUIDialogSettingsManagerBase::SetupView();

  // set heading
  SetHeading(m_source != nullptr ? 39700 : 39701);

  // set control labels
  SET_CONTROL_LABEL(CONTROL_SETTINGS_OKAY_BUTTON, 186);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 222);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CUSTOM_BUTTON, 409);
}

std::shared_ptr<CSettingSection> CGUIDialogMediaImportInfo::GetSection()
{
  // for media sources with settings we provide a custom section
  if (m_source != nullptr && m_source->Settings()->IsLoaded())
  {
    const auto sections = m_source->Settings()->GetSections();
    if (!sections.empty())
      return sections.front();
  }
  else if (m_import != nullptr && m_import->Settings()->IsLoaded())
  {
    const auto sections = m_import->Settings()->GetSections();
    if (!sections.empty())
      return sections.front();
  }

  return nullptr;
}

void CGUIDialogMediaImportInfo::Save()
{
  if (m_import != nullptr)
  {
    bool success = false;
    if (m_importer->UnloadImportSettings(*m_import))
      success = CServiceBroker::GetMediaImportManager().UpdateImport(*m_import);

    if (success)
      CLog::Log(LOGINFO, "CGUIDialogMediaImportInfo: settings for import {} saved", *m_import);
    else
      CLog::Log(LOGERROR, "CGUIDialogMediaImportInfo: failed to save settings for import {}", *m_import);
  }
  else if (m_source != nullptr)
  {
    bool success = false;
    if (m_importer->UnloadSourceSettings(*m_source))
      success = CServiceBroker::GetMediaImportManager().UpdateSource(*m_source);

    if (success)
      CLog::Log(LOGINFO, "CGUIDialogMediaImportInfo: settings for source {} saved", *m_source);
    else
      CLog::Log(LOGINFO, "CGUIDialogMediaImportInfo: failed to save settings for source {}", *m_source);
  }
}

CSettingsManager* CGUIDialogMediaImportInfo::GetSettingsManager() const
{
  // for media sources with settings we provide a custom section
  if (m_source != nullptr && m_source->Settings()->IsLoaded())
    return m_source->Settings()->GetSettingsManager();
  else if (m_import != nullptr && m_import->Settings()->IsLoaded())
    return m_import->Settings()->GetSettingsManager();

  return nullptr;
}

void CGUIDialogMediaImportInfo::InitializeSettings()
{
  if (m_import != nullptr)
    InitializeMediaImportSettings();
  else if (m_source != nullptr)
    InitializeMediaImportSourceSettings();
}

void CGUIDialogMediaImportInfo::InitializeMediaImportSettings()
{
  if (m_importer == nullptr)
    return;

  if (!m_importer->LoadImportSettings(*m_import))
    return;
}

void CGUIDialogMediaImportInfo::InitializeMediaImportSourceSettings()
{
  if (m_importer == nullptr)
    return;

  if (!m_importer->LoadSourceSettings(*m_source))
    return;
}

bool CGUIDialogMediaImportInfo::SetMediaImport(const CFileItemPtr& item)
{
  if (!item->HasProperty(PROPERTY_SOURCE_IDENTIFIER) ||
      !item->HasProperty(PROPERTY_IMPORT_PATH) ||
      !item->HasProperty(PROPERTY_IMPORT_MEDIATYPES))
    return false;

  std::string importPath = item->GetProperty(PROPERTY_IMPORT_PATH).asString();
  GroupedMediaTypes mediaTypes = CMediaTypes::Split(item->GetProperty(PROPERTY_IMPORT_MEDIATYPES).asString());
  if (importPath.empty() || mediaTypes.empty())
    return false;

  // get the import details
  m_import = std::make_shared<CMediaImport>();
  if (!CServiceBroker::GetMediaImportManager().GetImport(importPath, mediaTypes, *m_import))
  {
    m_import.reset();
    return false;
  }

  // get a matching importer
  m_importer = CServiceBroker::GetMediaImportManager().GetImporterBySource(m_import->GetSource());
  if (m_importer == nullptr)
    return false;

  // copy the given item
  *m_item = *item;

  return true;
}

bool CGUIDialogMediaImportInfo::SetMediaImportSource(const CFileItemPtr& item)
{
  std::string sourceId = item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString();
  if (sourceId.empty())
    return false;

  m_source = std::make_shared<CMediaImportSource>(sourceId);
  if (!CServiceBroker::GetMediaImportManager().GetSource(sourceId, *m_source))
  {
    m_source.reset();
    return false;
  }

  // get a matching importer
  m_importer = CServiceBroker::GetMediaImportManager().GetImporterBySource(*m_source);
  if (m_importer == nullptr)
    return false;

  // copy the given item
  *m_item = *item;

  return true;
}
