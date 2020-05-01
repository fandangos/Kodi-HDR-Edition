/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIWindowMediaSourceBrowser.h"

#include "FileItem.h"
#include "GUIUserMessages.h"
#include "ServiceBroker.h"
#include "URL.h"
#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogProgress.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/MediaImportDirectory.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIKeyboardFactory.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/WindowIDs.h"
#include "input/Key.h"
#include "media/MediaType.h"
#include "media/import/MediaImportManager.h"
#include "media/import/dialogs/GUIDialogMediaImportInfo.h"
#include "messaging/helpers/DialogOKHelper.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

#include <algorithm>

#include <fmt/ostream.h>

using namespace std;

CGUIWindowMediaSourceBrowser::CGUIWindowMediaSourceBrowser()
  : CGUIMediaWindow(WINDOW_MEDIASOURCE_BROWSER, "MediaSourceBrowser.xml"),
    m_logger(CServiceBroker::GetLogging().GetLogger("CGUIWindowMediaSourceBrowser"))
{
}

std::string CGUIWindowMediaSourceBrowser::ShowAndGetImporterToLookup(
    const std::vector<MediaImporterPtr>& importers)
{
  if (importers.empty())
    return "";

  // show the select dialog with all the importers which support source lookups
  auto selectDialog = static_cast<CGUIDialogSelect*>(
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_SELECT));
  if (selectDialog == nullptr)
    return "";

  selectDialog->Reset();
  selectDialog->SetHeading(39600);
  selectDialog->SetMultiSelection(false);

  for (const auto& importer : importers)
  {
    CFileItem item(importer->GetSourceLookupProtocol());
    item.SetPath(importer->GetIdentification()); // abuse the identification of the importer
    selectDialog->Add(item);
  }

  selectDialog->Open();

  if (!selectDialog->IsConfirmed() || selectDialog->GetSelectedFileItem() == nullptr)
    return "";

  return selectDialog->GetSelectedFileItem()->GetPath();
}

std::string CGUIWindowMediaSourceBrowser::ShowAndGetMediaSourcesToImportFrom(
    const GroupedMediaTypes& mediaTypes /* = GroupedMediaTypes() */)
{
  // get all available sources
  std::vector<CMediaImportSource> sources = CServiceBroker::GetMediaImportManager().GetSources();

  for (std::vector<CMediaImportSource>::iterator source = sources.begin(); source != sources.end();)
  {
    // get all imports for the source
    auto imports =
        CServiceBroker::GetMediaImportManager().GetImportsBySource(source->GetIdentifier());

    // put together all already imported media types for the source
    std::set<MediaType> importedMediaTypes;
    for (const auto& import : imports)
      importedMediaTypes.insert(import.GetMediaTypes().begin(), import.GetMediaTypes().end());

    const auto& availableMediaTypes = source->GetAvailableMediaTypes();
    if (!source->IsActive() ||
        std::all_of(availableMediaTypes.begin(), availableMediaTypes.end(),
                    [&importedMediaTypes](const MediaType& mediaType) -> bool {
                      return importedMediaTypes.find(mediaType) != importedMediaTypes.end();
                    }))
    {
      source = sources.erase(source);
      continue;
    }

    ++source;
  }

  if (sources.empty())
    return "";

  // show the select dialog with all the sources from which we can import
  auto selectDialog = static_cast<CGUIDialogSelect*>(
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_SELECT));
  if (selectDialog == nullptr)
    return "";

  selectDialog->Reset();
  selectDialog->SetHeading(39600);
  selectDialog->SetMultiSelection(false);

  for (const auto& source : sources)
  {
    CFileItem item(source.GetFriendlyName());
    item.SetPath(source.GetIdentifier()); // abuse the path for the media source's identification

    selectDialog->Add(item);
  }

  selectDialog->Open();

  if (!selectDialog->IsConfirmed() || selectDialog->GetSelectedFileItem() == nullptr)
    return "";

  return selectDialog->GetSelectedFileItem()->GetPath();
}

GroupedMediaTypes CGUIWindowMediaSourceBrowser::ShowAndGetMediaTypesToImport(
    const std::string& sourceID)
{
  GroupedMediaTypes mediaTypesToImport;

  if (sourceID.empty())
    return mediaTypesToImport;

  CMediaImportSource source(sourceID);
  if (!CServiceBroker::GetMediaImportManager().GetSource(sourceID, source))
    return mediaTypesToImport;

  return ShowAndGetMediaTypesToImport(source);
}

GroupedMediaTypes CGUIWindowMediaSourceBrowser::ShowAndGetMediaTypesToImport(
    const CMediaImportSource& source)
{
  GroupedMediaTypes mediaTypesToImport;

  if (source.GetIdentifier().empty() || source.GetFriendlyName().empty() ||
      source.GetAvailableMediaTypes().empty())
    return mediaTypesToImport;

  // put together a list of media types that are available for the source and haven't been imported yet
  MediaTypes unimportedMediaTypes = source.GetAvailableMediaTypes();
  std::vector<CMediaImport> imports =
      CServiceBroker::GetMediaImportManager().GetImportsBySource(source.GetIdentifier());
  for (const auto& import : imports)
  {
    for (const auto& mediaType : import.GetMediaTypes())
      unimportedMediaTypes.erase(mediaType);
  }

  // show the select dialog with all the media types available for import
  auto selectDialog = static_cast<CGUIDialogSelect*>(
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_SELECT));
  if (selectDialog != nullptr)
  {
    selectDialog->Reset();
    selectDialog->SetHeading(39555);
    selectDialog->SetMultiSelection(false);

    CFileItemList items;
    if (!GetMediaTypesToImport(unimportedMediaTypes, items))
      return mediaTypesToImport;

    selectDialog->SetItems(items);
    selectDialog->Open();

    if (selectDialog->IsConfirmed() && selectDialog->GetSelectedFileItem() != nullptr)
      return StringUtils::Split(selectDialog->GetSelectedFileItem()->GetPath(), ",");
  }

  return mediaTypesToImport;
}

bool CGUIWindowMediaSourceBrowser::GetMediaTypesToImport(const MediaTypes& availableMediaTypes,
                                                         CFileItemList& items)
{
  if (availableMediaTypes.empty())
    return false;

  std::vector<GroupedMediaTypes> supportedMediaTypes =
      CServiceBroker::GetMediaImportManager().GetSupportedMediaTypesGrouped(availableMediaTypes);
  if (supportedMediaTypes.empty())
    return false;

  for (const auto& itSupportedMediaType : supportedMediaTypes)
  {
    CFileItemPtr pItem(new CFileItem(CMediaTypes::ToLabel(itSupportedMediaType)));
    pItem->SetPath(CMediaTypes::Join(
        itSupportedMediaType)); // abuse the path for the media type's identification

    items.Add(pItem);
  }

  return true;
}

bool CGUIWindowMediaSourceBrowser::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
    case GUI_MSG_WINDOW_INIT:
    {
      m_rootDir.AllowNonLocalSources(false);

      // is this the first time the window is opened?
      if (m_vecItems->GetPath() == "?" && message.GetStringParam().empty())
        m_vecItems->SetPath("");
      break;
    }

    case GUI_MSG_WINDOW_DEINIT:
    {
      break;
    }

    case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      if (m_viewControl.HasControl(iControl)) // list/thumb control
      {
        // get selected item
        int itemIndex = m_viewControl.GetSelectedItem();
        int actionId = message.GetParam1();

        if (actionId == ACTION_SHOW_INFO || actionId == ACTION_DELETE_ITEM)
        {
          CFileItemPtr item = m_vecItems->Get(itemIndex);
          if (item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).empty())
            return false;

          if (item->GetProperty(PROPERTY_IMPORT_PATH).empty())
          {
            if (actionId == ACTION_SHOW_INFO)
              return OnSourceInfo(item);
            else if (actionId == ACTION_DELETE_ITEM)
              return OnSourceDelete(item);
          }
          else
          {
            if (actionId == ACTION_SHOW_INFO)
              return OnImportInfo(item);
            else if (actionId == ACTION_DELETE_ITEM)
              return OnImportDelete(item);
          }
        }
      }
      break;
    }

    case GUI_MSG_NOTIFY_ALL:
    {
      CGUIListItemPtr item = message.GetItem();
      if (item == nullptr)
        break;

      switch (message.GetParam1())
      {
        case GUI_MSG_SOURCE_ADDED:
        {
          OnSourceAdded(message.GetItem()->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString());
          return true;
        }

        case GUI_MSG_SOURCE_UPDATED:
        {
          OnSourceUpdated(message.GetItem()->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString());
          return true;
        }

        case GUI_MSG_SOURCE_REMOVED:
        {
          OnSourceRemoved(message.GetItem()->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString());
          return true;
        }

        case GUI_MSG_SOURCE_ACTIVE_CHANGED:
        {
          OnSourceIsActiveChanged(
              message.GetItem()->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString(),
              message.GetParam1() > 0);
          return true;
        }

        case GUI_MSG_IMPORT_ADDED:
        {
          OnImportAdded(
              item->GetProperty(PROPERTY_IMPORT_PATH).asString(),
              CMediaTypes::Split(item->GetProperty(PROPERTY_IMPORT_MEDIATYPES).asString()));
          return true;
        }

        case GUI_MSG_IMPORT_UPDATED:
        {
          OnImportUpdated(
              item->GetProperty(PROPERTY_IMPORT_PATH).asString(),
              CMediaTypes::Split(item->GetProperty(PROPERTY_IMPORT_MEDIATYPES).asString()));
          return true;
        }

        case GUI_MSG_IMPORT_REMOVED:
        {
          OnImportRemoved(
              item->GetProperty(PROPERTY_IMPORT_PATH).asString(),
              CMediaTypes::Split(item->GetProperty(PROPERTY_IMPORT_MEDIATYPES).asString()));
          return true;
        }

        default:
          break;
      }
    }

    default:
      break;
  }
  return CGUIMediaWindow::OnMessage(message);
}

bool CGUIWindowMediaSourceBrowser::RefreshList(bool keepSelectedItem /* = true */)
{
  std::string currentItemPath;
  if (keepSelectedItem)
  {
    int itemIndex = m_viewControl.GetSelectedItem();
    if (itemIndex >= 0 && itemIndex < m_vecItems->Size())
      currentItemPath = m_vecItems->Get(itemIndex)->GetPath();
  }

  bool result = Refresh(true);

  if (keepSelectedItem && !currentItemPath.empty())
    m_viewControl.SetSelectedItem(currentItemPath);

  return result;
}

bool CGUIWindowMediaSourceBrowser::OnSourceSynchronise(const CFileItemPtr& item)
{
  if (item == nullptr)
    return false;

  if (CServiceBroker::GetMediaImportManager().Import(
          item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString()))
    return true;

  KODI::MESSAGING::HELPERS::ShowOKDialogText(
      39610, StringUtils::Format(g_localizeStrings.Get(39611), item->GetLabel()));
  return false;
}

bool CGUIWindowMediaSourceBrowser::OnSourceInfo(const CFileItemPtr& item)
{
  if (item == nullptr)
    return false;

  // only active sources can be configured because the configuration might require interaction with the source
  if (!item->GetProperty(PROPERTY_SOURCE_ISACTIVE).asBoolean())
  {
    KODI::MESSAGING::HELPERS::ShowOKDialogText(
        39700, StringUtils::Format(g_localizeStrings.Get(39614), item->GetLabel()));
    return false;
  }

  return CGUIDialogMediaImportInfo::ShowForMediaImportSource(item);
}

bool CGUIWindowMediaSourceBrowser::OnSourceDelete(const CFileItemPtr& item)
{
  if (item == nullptr)
    return false;

  auto pDialog = static_cast<CGUIDialogYesNo*>(
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_YES_NO));
  if (pDialog == nullptr)
    return false;

  pDialog->SetHeading(39602);
  pDialog->SetText(StringUtils::Format(g_localizeStrings.Get(39603), item->GetLabel()));

  pDialog->Open();

  if (!pDialog->IsConfirmed())
    return false;

  CServiceBroker::GetMediaImportManager().RemoveSource(
      item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString());
  return true;
}

bool CGUIWindowMediaSourceBrowser::OnImportSynchronise(const CFileItemPtr& item)
{
  if (item == nullptr)
    return false;

  if (CServiceBroker::GetMediaImportManager().Import(
          item->GetProperty(PROPERTY_IMPORT_PATH).asString(),
          CMediaTypes::Split(item->GetProperty(PROPERTY_IMPORT_MEDIATYPES).asString())))
    return true;

  KODI::MESSAGING::HELPERS::ShowOKDialogText(
      39612, StringUtils::Format(g_localizeStrings.Get(39613), item->GetLabel()));
  return false;
}

bool CGUIWindowMediaSourceBrowser::OnImportInfo(const CFileItemPtr& item)
{
  if (item == nullptr)
    return false;

  // only imports with active and ready sources can be configured because the configuration might require interaction with the source
  if (!item->GetProperty(PROPERTY_SOURCE_ISACTIVE).asBoolean() ||
      !item->GetProperty(PROPERTY_SOURCE_ISREADY).asBoolean())
  {
    KODI::MESSAGING::HELPERS::ShowOKDialogText(
        39701, StringUtils::Format(g_localizeStrings.Get(39615), item->GetLabel()));
    return false;
  }

  return CGUIDialogMediaImportInfo::ShowForMediaImport(item);
}

bool CGUIWindowMediaSourceBrowser::OnImportDelete(const CFileItemPtr& item)
{
  if (item == nullptr)
    return false;

  auto pDialog = static_cast<CGUIDialogYesNo*>(
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_YES_NO));
  if (pDialog == nullptr)
    return false;

  std::string sourceID = item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString();
  std::string importPath = item->GetProperty(PROPERTY_IMPORT_PATH).asString();
  GroupedMediaTypes mediaTypes =
      CMediaTypes::Split(item->GetProperty(PROPERTY_IMPORT_MEDIATYPES).asString());

  pDialog->SetHeading(39604);
  pDialog->SetText(StringUtils::Format(g_localizeStrings.Get(39603), item->GetLabel()));

  pDialog->Open();

  if (!pDialog->IsConfirmed())
    return false;

  CServiceBroker::GetMediaImportManager().RemoveImport(importPath, mediaTypes);
  return true;
}

void CGUIWindowMediaSourceBrowser::OnSourceAdded(const std::string& sourceId)
{
  if (m_vecItems->GetContent() == "sources")
    RefreshList(true);
}

void CGUIWindowMediaSourceBrowser::OnSourceUpdated(const std::string& sourceId)
{
  if (m_vecItems->GetContent() == "sources")
  {
    // only refresh the list if the updated source is part of it
    CFileItemPtr item = GetSourceItem(sourceId);
    if (item != nullptr)
      RefreshList(true);
  }
}

void CGUIWindowMediaSourceBrowser::OnSourceRemoved(const std::string& sourceId)
{
  if (m_vecItems->GetContent() == "sources")
  {
    // only refresh the list if the removed source is part of it
    CFileItemPtr item = GetSourceItem(sourceId);
    if (item != nullptr)
    {
      int selectedItemIndex = m_viewControl.GetSelectedItem();
      // only update the selected item index when the deleted item is focused
      if (m_vecItems->Get(selectedItemIndex) != item)
        selectedItemIndex = -1;

      RefreshList(false);

      if (selectedItemIndex >= 0)
        m_viewControl.SetSelectedItem(selectedItemIndex);
    }
  }
}

void CGUIWindowMediaSourceBrowser::OnImportAdded(const std::string& importPath,
                                                 const GroupedMediaTypes& mediaTypes)
{
  if (m_vecItems->GetContent() == "imports")
    RefreshList(true);
}

void CGUIWindowMediaSourceBrowser::OnImportUpdated(const std::string& importPath,
                                                   const GroupedMediaTypes& mediaTypes)
{
  if (m_vecItems->GetContent() == "imports")
  {
    // only refresh the list if the updated import is part of it
    CFileItemPtr item = GetImportItem(importPath, mediaTypes);
    if (item != nullptr)
      RefreshList(true);
  }
}

void CGUIWindowMediaSourceBrowser::OnImportRemoved(const std::string& importPath,
                                                   const GroupedMediaTypes& mediaTypes)
{
  if (m_vecItems->GetContent() == "imports")
  {
    // only refresh the list if the removed import is part of it
    CFileItemPtr item = GetImportItem(importPath, mediaTypes);
    if (item != nullptr)
    {
      int selectedItemIndex = m_viewControl.GetSelectedItem();
      // only update the selected item index when the deleted item is focused
      if (m_vecItems->Get(selectedItemIndex) != item)
        selectedItemIndex = -1;

      RefreshList(false);

      if (selectedItemIndex >= 0)
        m_viewControl.SetSelectedItem(selectedItemIndex);
    }
  }
}

void CGUIWindowMediaSourceBrowser::OnSourceIsActiveChanged(const std::string& sourceId,
                                                           bool isactive)
{
  RefreshList(true);

  // if the source was just manually added by the user open the info dialog
  if (m_manuallyAddedSourceId == sourceId)
  {
    m_manuallyAddedSourceId.clear();

    CMediaImportSource addedSource;
    if (CServiceBroker::GetMediaImportManager().GetSource(sourceId, addedSource))
    {
      CFileItemPtr sourceItem = XFILE::CMediaImportDirectory::FileItemFromMediaImportSource(
          addedSource, m_vecItems->GetPath());
      if (sourceItem != nullptr)
      {
        auto selectedItemPath = m_viewControl.GetSelectedItemPath();
        // now select the new import so that it will be available to skins in the source info dialog
        m_viewControl.SetSelectedItem(sourceItem->GetPath());

        // and now finally open the source info dialog
        OnSourceInfo(sourceItem);

        // now see if the add source button is still there and if so re-select it
        if (m_vecItems->Contains(selectedItemPath))
          m_viewControl.SetSelectedItem(selectedItemPath);
      }
    }
  }
}

CFileItemPtr CGUIWindowMediaSourceBrowser::GetImportItem(const std::string& importPath,
                                                         const GroupedMediaTypes& mediaTypes) const
{
  for (int index = 0; index < m_vecItems->Size(); index++)
  {
    CFileItemPtr item = m_vecItems->Get(index);
    if (item->IsParentFolder())
      continue;

    if (item->GetProperty(PROPERTY_IMPORT_PATH).asString() == importPath &&
        CMediaTypes::Split(item->GetProperty(PROPERTY_IMPORT_MEDIATYPES).asString()) == mediaTypes)
      return item;
  }

  return CFileItemPtr();
}

CFileItemPtr CGUIWindowMediaSourceBrowser::GetSourceItem(const std::string& sourceId) const
{
  for (int index = 0; index < m_vecItems->Size(); index++)
  {
    CFileItemPtr item = m_vecItems->Get(index);
    if (item->IsParentFolder())
      continue;

    if (item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString() == sourceId)
      return item;
  }

  return CFileItemPtr();
}

bool CGUIWindowMediaSourceBrowser::GetDirectory(const std::string& strDirectory,
                                                CFileItemList& items)
{
  if (!CGUIMediaWindow::GetDirectory(strDirectory, items))
    return false;

  CURL url(strDirectory);
  // "Add import" button for imports by media provider views
  if (items.GetContent() == "imports")
  {
    bool addNewImport = false;
    std::string addImportPath = "newimport://";
    std::string sourceDir = strDirectory;
    URIUtils::RemoveSlashAtEnd(sourceDir);
    std::string sourceID;
    if (url.GetHostName() == "all" || url.GetHostName() == "active")
      sourceID = url.GetFileName();
    else if (url.GetHostName() != "inactive")
      sourceID = url.GetHostName();
    URIUtils::RemoveSlashAtEnd(sourceID);
    sourceID = CURL::Decode(sourceID);

    addImportPath += CURL::Encode(sourceID);
    CMediaImportSource source(sourceID);
    if (CServiceBroker::GetMediaImportManager().GetSource(sourceID, source) && source.IsActive() &&
        CServiceBroker::GetMediaImportManager().IsSourceReady(source))
    {
      // count the number of media types already being imported for a source
      size_t importedMediaTypesCount = 0;
      for (const auto& import :
           CServiceBroker::GetMediaImportManager().GetImportsBySource(sourceID))
        importedMediaTypesCount += import.GetMediaTypes().size();

      // check if all media types are already being imported or not
      if (importedMediaTypesCount < source.GetAvailableMediaTypes().size())
        addNewImport = true;
    }

    if (addNewImport && !items.Contains(addImportPath))
    {
      CFileItemPtr addImport(new CFileItem(addImportPath, false));
      addImport->SetLabel(g_localizeStrings.Get(39606));
      addImport->SetLabelPreformatted(true);
      addImport->SetSpecialSort(SortSpecialOnBottom);
      items.Add(addImport);
    }
  }
  else if (items.GetContent() == "sources")
  {
    bool addNewSource = false;
    std::string addSourcePath = "newsource://";
    if (url.GetHostName() == "all" || url.GetHostName() == "active")
    {
      const auto importers = CServiceBroker::GetMediaImportManager().GetImporters();
      addNewSource = std::any_of(importers.begin(), importers.end(),
                                 [](const MediaImporterFactoryConstPtr& importer) {
                                   return importer->CreateImporter()->CanLookupSource();
                                 });
    }

    if (addNewSource && !items.Contains(addSourcePath))
    {
      CFileItemPtr addSource(new CFileItem(addSourcePath, false));
      addSource->SetLabel(g_localizeStrings.Get(39605));
      addSource->SetLabelPreformatted(true);
      addSource->SetSpecialSort(SortSpecialOnBottom);
      items.Add(addSource);
    }
  }

  return true;
}

std::string CGUIWindowMediaSourceBrowser::GetStartFolder(const std::string& dir)
{
  if (StringUtils::StartsWithNoCase(dir, "import://"))
    return dir;

  return CGUIMediaWindow::GetStartFolder(dir);
}

void CGUIWindowMediaSourceBrowser::GetContextButtons(int itemNumber, CContextButtons& buttons)
{
  CFileItemPtr item = m_vecItems->Get(itemNumber);
  const std::string sourceId = item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString();
  bool isSource = !sourceId.empty() && !item->HasProperty(PROPERTY_IMPORT_PATH);
  bool isImport = !sourceId.empty() && item->HasProperty(PROPERTY_IMPORT_PATH) &&
                  item->HasProperty(PROPERTY_IMPORT_MEDIATYPES);
  bool isActive = item->GetProperty(PROPERTY_SOURCE_ISACTIVE).asBoolean();
  bool isReady = item->GetProperty(PROPERTY_SOURCE_ISREADY).asBoolean();

  if (!isSource && !isImport)
    return;

  if (isActive)
  {
    // only allow synchronisation of the source is active and ready and has imports
    if (isReady &&
        (isImport || (isSource && CServiceBroker::GetMediaImportManager().HasImports(sourceId))))
      buttons.Add(CONTEXT_BUTTON_SCAN, g_localizeStrings.Get(39607));

    // only allow to open the info dialog for (active) sources and for (active and) ready imports
    if (isSource || isReady)
      buttons.Add(CONTEXT_BUTTON_INFO, g_localizeStrings.Get(19033));
  }

  buttons.Add(CONTEXT_BUTTON_DELETE, g_localizeStrings.Get(15015));
}

bool CGUIWindowMediaSourceBrowser::OnContextButton(int itemNumber, CONTEXT_BUTTON button)
{
  CFileItemPtr item = m_vecItems->Get(itemNumber);
  bool isSource = !item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).empty() &&
                  !item->HasProperty(PROPERTY_IMPORT_PATH);

  switch (button)
  {
    case CONTEXT_BUTTON_SCAN:
    {
      if (isSource)
        return OnSourceSynchronise(item);
      else
        return OnImportSynchronise(item);
      break;
    }

    case CONTEXT_BUTTON_INFO:
    {
      if (isSource)
        return OnSourceInfo(item);
      else
        return OnImportInfo(item);
      break;
    }

    case CONTEXT_BUTTON_DELETE:
    {
      if (isSource)
        return OnSourceDelete(item);
      else
        return OnImportDelete(item);
      break;
    }

    default:
      break;
  }

  return CGUIMediaWindow::OnContextButton(itemNumber, button);
}

bool CGUIWindowMediaSourceBrowser::OnClick(int iItem, const std::string& player /* = "" */)
{
  CFileItemPtr item = m_vecItems->Get(iItem);
  CURL url(item->GetPath());

  auto& mediaImportManager = CServiceBroker::GetMediaImportManager();

  if (url.GetProtocol() == "newimport")
    return AddNewImport(item, url);

  if (url.GetProtocol() == "newsource")
    return AddNewSource(item, url);

  const std::string sourceId = item->GetProperty(PROPERTY_SOURCE_IDENTIFIER).asString();
  if (!item->m_bIsFolder)
  {
    // if the item is a media import show the info dialog
    if (!sourceId.empty() && item->HasProperty(PROPERTY_IMPORT_PATH) &&
        item->HasProperty(PROPERTY_IMPORT_MEDIATYPES))
      return OnImportInfo(item);
  }
  else if (!sourceId.empty())
  {
    // if the source is active but not ready and does not have any imports open the info dialog
    if (item->GetProperty(PROPERTY_SOURCE_ISACTIVE).asBoolean() &&
        !item->GetProperty(PROPERTY_SOURCE_ISREADY).asBoolean() &&
        !mediaImportManager.HasImports(sourceId))
      return OnSourceInfo(item);
  }

  return CGUIMediaWindow::OnClick(iItem);
}

bool CGUIWindowMediaSourceBrowser::AddNewImport(CFileItemPtr newImportItem, const CURL& url)
{
  std::string sourceID = CURL::Decode(url.GetHostName());
  if (sourceID.empty())
    return false;

  // ask the user what media type to add as an import
  GroupedMediaTypes mediaTypesToImport = ShowAndGetMediaTypesToImport(sourceID);
  if (mediaTypesToImport.empty())
    return true;

  auto& mediaImportManager = CServiceBroker::GetMediaImportManager();

  // add the new import (they aren't automatically synchronised)
  if (!mediaImportManager.AddRecursiveImport(sourceID, sourceID, mediaTypesToImport))
  {
    KODI::MESSAGING::HELPERS::ShowOKDialogText(39608, g_localizeStrings.Get(39609));
    return false;
  }

  // show the info dialog for the new import
  CMediaImport import;
  if (mediaImportManager.GetImport(sourceID, mediaTypesToImport, import))
  {
    CFileItemPtr importItem =
        XFILE::CMediaImportDirectory::FileItemFromMediaImport(import, m_vecItems->GetPath());
    if (importItem != nullptr)
    {
      // refresh the list so that the new import is listed
      RefreshList(false);

      // now select the new import so that it will be available to skins in the import info dialog
      m_viewControl.SetSelectedItem(importItem->GetPath());

      // and now finally open the import info dialog
      OnImportInfo(importItem);

      // now see if the add import button is still there and if so re-select it
      if (m_vecItems->Contains(newImportItem->GetPath()))
        m_viewControl.SetSelectedItem(newImportItem->GetPath());
    }
  }

  // now try to synchronise the newly added import
  mediaImportManager.Import(sourceID, mediaTypesToImport);

  return true;
}

bool CGUIWindowMediaSourceBrowser::AddNewSource(CFileItemPtr newImportItem, const CURL& url)
{
  auto& mediaImportManager = CServiceBroker::GetMediaImportManager();

  // get all importers which support source lookup
  auto importerFactories = mediaImportManager.GetImporters();
  std::vector<MediaImporterPtr> importers;
  for (const auto& importerFactory : importerFactories)
  {
    auto importer = importerFactory->CreateImporter();
    if (importer->CanLookupSource())
      importers.push_back(std::move(importer));
  }

  const auto importerId = ShowAndGetImporterToLookup(importers);
  if (importerId.empty())
    return true;

  const auto selectedImporter = std::find_if(importers.begin(), importers.end(),
                                             [importerId](const MediaImporterPtr& importer) {
                                               return importer->GetIdentification() == importerId;
                                             });
  if (selectedImporter == importers.end())
    return true;

  CMediaImportSource discoveredSource;
  if (!mediaImportManager.DiscoverSource(importerId, discoveredSource))
  {
    m_logger->warn("failed to discover source with importer \"{}\"", importerId);
    KODI::MESSAGING::HELPERS::ShowOKDialogText(
        CVariant{(*selectedImporter)->GetSourceLookupProtocol()},
        CVariant{g_localizeStrings.Get(39618)});
    return false;
  }

  CMediaImportSource existingSource;
  if (mediaImportManager.GetSource(discoveredSource.GetIdentifier(), existingSource))
  {
    m_logger->info("source {} at \"{}\" already exists", discoveredSource,
                   discoveredSource.GetBasePath());
    KODI::MESSAGING::HELPERS::ShowOKDialogText(
        CVariant{(*selectedImporter)->GetSourceLookupProtocol()},
        CVariant{
            StringUtils::Format(g_localizeStrings.Get(39619), discoveredSource.GetFriendlyName())});
    return true;
  }

  bool result = mediaImportManager.AddAndActivateSourceManually(discoveredSource);
  if (!result)
  {
    m_logger->error("failed to add discovered source {} at \"{}\"", discoveredSource,
                    discoveredSource.GetBasePath());
    KODI::MESSAGING::HELPERS::ShowOKDialogText(
        CVariant{(*selectedImporter)->GetSourceLookupProtocol()},
        CVariant{g_localizeStrings.Get(39618)});
    return false;
  }

  m_manuallyAddedSourceId = discoveredSource.GetIdentifier();
  return true;
}
