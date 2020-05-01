/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIViewStateMediaSourceBrowser.h"

#include "FileItem.h"
#include "URL.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/WindowIDs.h"
#include "view/ViewState.h"
#include "windowing/GraphicContext.h"

CGUIViewStateMediaSourceBrowser::CGUIViewStateMediaSourceBrowser(const CFileItemList& items)
  : CGUIViewState(items)
{
  CURL url(items.GetPath());

  if (items.GetContent() == "sources" || items.GetContent() == "imports")
  {
    AddSortMethod(
        SortByLabel, SortAttributeIgnoreFolders, 551,
        LABEL_MASKS("%L", "%d", "%L", "%d")); // Filename, Date Time | Foldername, Date Time
    AddSortMethod(
        SortByDate, SortAttributeIgnoreFolders, 579,
        LABEL_MASKS("%L", "%d", "%L", "%d")); // Filename, Date Time | Foldername, Date Time

    SetSortMethod(SortByLabel);
    SetSortOrder(SortOrderAscending);
  }
  else
  {
    SetSortMethod(SortByNone);
    SetSortOrder(SortOrderNone);
  }

  SetViewAsControl(DEFAULT_VIEW_AUTO);

  LoadViewState(items.GetPath(), WINDOW_MEDIASOURCE_BROWSER);
}

void CGUIViewStateMediaSourceBrowser::SaveViewState()
{
  SaveViewToDb(m_items.GetPath(), WINDOW_MEDIASOURCE_BROWSER);
}

std::string CGUIViewStateMediaSourceBrowser::GetExtensions()
{
  return "";
}

VECSOURCES& CGUIViewStateMediaSourceBrowser::GetSources()
{
  m_sources.clear();

  // all sources
  {
    CMediaSource source;
    source.strPath = "import://all/";
    source.m_iDriveType = CMediaSource::SOURCE_TYPE_LOCAL;
    source.strName = g_localizeStrings.Get(39573);
    m_sources.push_back(source);
  }

  // active sources
  {
    CMediaSource source;
    source.strPath = "import://active/";
    source.m_iDriveType = CMediaSource::SOURCE_TYPE_LOCAL;
    source.strName = g_localizeStrings.Get(39574);
    m_sources.push_back(source);
  }

  // inactive sources
  {
    CMediaSource source;
    source.strPath = "import://inactive/";
    source.m_iDriveType = CMediaSource::SOURCE_TYPE_LOCAL;
    source.strName = g_localizeStrings.Get(39575);
    m_sources.push_back(source);
  }

  return CGUIViewState::GetSources();
}
