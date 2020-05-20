/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportEvent.h"

#include "ServiceBroker.h"
#include "URL.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/WindowIDs.h"
#include "media/import/MediaImportManager.h"
#include "utils/StringUtils.h"

CMediaImportSourceEvent::CMediaImportSourceEvent(const CMediaImportSource& source,
                                                 const CVariant& description,
                                                 EventLevel level /* = EventLevel::Information */)
  : CMediaImportSourceEvent(source, description, false, level)
{
}

CMediaImportSourceEvent::CMediaImportSourceEvent(const CMediaImportSource& source,
                                                 const CVariant& description,
                                                 bool removed,
                                                 EventLevel level /* = EventLevel::Information */)
  : CUniqueEvent(
        source.GetFriendlyName(), description, source.GetIconUrl(), CVariant{removed}, level),
    m_source(source)
{
}

std::string CMediaImportSourceEvent::GetExecutionLabel() const
{
  std::string executionLabel = CUniqueEvent::GetExecutionLabel();
  if (!executionLabel.empty())
    return executionLabel;

  return g_localizeStrings.Get(39552);
}

bool CMediaImportSourceEvent::CanExecute() const
{
  return !m_details.isBoolean() || !m_details.asBoolean();
}

bool CMediaImportSourceEvent::Execute() const
{
  if (!CanExecute())
    return false;

  std::vector<std::string> params;
  params.push_back(
      StringUtils::Format("import://%s/", CURL::Encode(m_source.GetIdentifier()).c_str()));
  params.push_back("return");
  CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_MEDIASOURCE_BROWSER, params);
  return true;
}

CMediaImportEvent::CMediaImportEvent(const CMediaImport& import,
                                     const CVariant& description,
                                     EventLevel level /* = EventLevel::Information */)
  : CMediaImportEvent(import, description, false, level)
{
}

CMediaImportEvent::CMediaImportEvent(const CMediaImport& import,
                                     const CVariant& description,
                                     bool removed,
                                     EventLevel level /* = EventLevel::Information */)
  : CUniqueEvent(StringUtils::Format(g_localizeStrings.Get(39565).c_str(),
                                     import.GetSource().GetFriendlyName().c_str(),
                                     CMediaTypes::ToLabel(import.GetMediaTypes()).c_str()),
                 description,
                 import.GetSource().GetIconUrl(),
                 CVariant{removed},
                 level),
    m_import(import)
{
}

std::string CMediaImportEvent::GetExecutionLabel() const
{
  std::string executionLabel = CUniqueEvent::GetExecutionLabel();
  if (!executionLabel.empty())
    return executionLabel;

  return g_localizeStrings.Get(39607);
}

bool CMediaImportEvent::CanExecute() const
{
  return !m_details.isBoolean() || !m_details.asBoolean();
}

bool CMediaImportEvent::Execute() const
{
  if (!CanExecute())
    return false;

  return CServiceBroker::GetMediaImportManager().Import(m_import.GetPath(),
                                                        m_import.GetMediaTypes());
}
