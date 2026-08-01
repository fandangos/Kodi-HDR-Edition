/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#if HAS_DS_PLAYER

#include "FilterCoreFactory.h"
#include "DSPlayer.h"
#include "filters/DSBluraySource.h"
#include "filters/XBMCFileSource.h"
#include "utils/URIUtils.h"
#include "guilib/GUIComponent.h"

InternalFilters internalFilters[] =
{
  { "internal_archivesource", "Internal archive source", &InternalFilterConstructor < CXBMCASyncReader > },
  { "internal_bluraysource", "Internal Blu-ray source", &InternalFilterConstructor < CDSBlurayReader > }
};

HRESULT CFilterCoreFactory::LoadMediasConfiguration(TiXmlElement* pConfig, int iPriority)
{
  if (!pConfig || strcmpi(pConfig->Value(), "mediasconfig") != 0)
  {
    CLog::Log(LOGERROR, "Error loading medias configuration, no <mediasconfig> node");
    return E_FAIL;
  }

  /* Parse selection rules */
  TiXmlElement *pRules = pConfig->FirstChildElement("rules");
  if (pRules)
  {
    TiXmlElement *pRule = pRules->FirstChildElement("rule");
    while (pRule)
    {
      m_selecRules.push_back(new CGlobalFilterSelectionRule(pRule, iPriority));
      pRule = pRule->NextSiblingElement("rule");
    }
  }

  return S_OK;
}

HRESULT CFilterCoreFactory::LoadFiltersConfiguration(TiXmlElement* pConfig)
{
  if (!pConfig || strcmpi(pConfig->Value(), "filtersconfig") != 0)
  {
    CLog::Log(LOGERROR, "Error loading filters configuration, no <filtersconfig> node");
    return E_FAIL;
  }

  /* Parse filters declaration */

  TiXmlElement *pFilters = pConfig->FirstChildElement("filters");
  if (pFilters)
  {
    TiXmlElement *pFilter = pFilters->FirstChildElement("filter");
    std::string type = "";
    while (pFilter)
    {
      m_Filters.push_back(new CFGFilterFile(pFilter));
      pFilter = pFilter->NextSiblingElement("filter");
    }
  }

  return S_OK;
}

void CFilterCoreFactory::DebugRules()
{
  for (std::vector<CGlobalFilterSelectionRule*>::iterator it = m_selecRules.begin(); it != m_selecRules.end(); it++)
  {
    CLog::Log(LOGINFO,"{}",(*it)->GetRuleInfo().c_str());
  }
}

CGlobalFilterSelectionRule* CFilterCoreFactory::GetGlobalFilterSelectionRule(const CFileItem& pFileItem, bool checkUrl /*= false*/)
{
  std::sort(m_selecRules.begin(), m_selecRules.end(), compare_by_word);

  for (ULONG i = 0; i < m_selecRules.size(); i++)
  {
    if (m_selecRules[i]->Match(pFileItem, checkUrl))
      return m_selecRules[i];
  }

  return NULL;
}

HRESULT CFilterCoreFactory::GetSourceFilter(const CFileItem& pFileItem, std::string& filter)
{
  filter = "";

  /* Special case for internet stream, and rar archive */
  // TODO: handle DVD
  if (URIUtils::IsInArchive(pFileItem.GetPath()))
  {
    filter = "internal_archivesource";
    return S_OK;
  }

  CGlobalFilterSelectionRule * pRule = GetGlobalFilterSelectionRule(pFileItem, true);
  if (!pRule)
  {
    if (URIUtils::IsInternetStream(pFileItem.GetDynPath()))
    {
      filter = CGraphFilters::INTERNAL_LAVSPLITTER;
      return S_OK;
    }
    return E_FAIL;
  }

  std::vector<std::string> foo;
  pRule->GetSourceFilters(pFileItem, foo);

  if (foo.empty())
    return E_FAIL;

  filter = foo[0];
  return S_OK;
}

HRESULT CFilterCoreFactory::GetSplitterFilter(const CFileItem& pFileItem, std::string& filter)
{
  filter = "";
  CGlobalFilterSelectionRule * pRule = GetGlobalFilterSelectionRule(pFileItem);
  if (!pRule)
    return E_FAIL;

  std::vector<std::string> foo;
  pRule->GetSplitterFilters(pFileItem, foo);

  if (foo.empty())
    return E_FAIL;

  filter = foo[0];
  return S_OK;
}

HRESULT CFilterCoreFactory::GetAudioRendererFilter(const CFileItem& pFileItem, std::string& filter)
{
  filter = "";
  CGlobalFilterSelectionRule * pRule = GetGlobalFilterSelectionRule(pFileItem);
  if (!pRule)
    return E_FAIL;

  std::vector<std::string> foo;
  pRule->GetAudioRendererFilters(pFileItem, foo);

  if (foo.empty())
    return E_FAIL;

  filter = foo[0];
  return S_OK;
}

HRESULT CFilterCoreFactory::GetAudioFilter(const CFileItem& pFileItem, std::string& filter, bool dxva /*= false*/)
{
  filter = "";
  CGlobalFilterSelectionRule * pRule = GetGlobalFilterSelectionRule(pFileItem);
  if (!pRule)
    return E_FAIL;

  std::vector<std::string> foo;
  pRule->GetAudioFilters(pFileItem, foo, dxva);

  if (foo.empty())
    return E_FAIL;

  filter = foo[0];
  return S_OK;;
}

HRESULT CFilterCoreFactory::GetVideoFilter(const CFileItem& pFileItem, std::string& filter, bool dxva /*= false*/)
{
  filter = "";
  CGlobalFilterSelectionRule * pRule = GetGlobalFilterSelectionRule(pFileItem);
  if (!pRule)
    return E_FAIL;

  std::vector<std::string> foo;
  pRule->GetVideoFilters(pFileItem, foo, dxva);

  if (foo.empty())
    return E_FAIL; //Todo: Error message

  filter = foo[0];
  return S_OK;
}

HRESULT CFilterCoreFactory::GetSubsFilter(const CFileItem& pFileItem, std::string& filter, bool dxva /*= false*/)
{
  filter = "";
  CGlobalFilterSelectionRule * pRule = GetGlobalFilterSelectionRule(pFileItem);
  if (!pRule)
    return E_FAIL;

  std::vector<std::string> foo;
  pRule->GetSubsFilters(pFileItem, foo, dxva);

  if (foo.empty())
    return E_FAIL; //Todo: Error message

  filter = foo[0];
  return S_OK;
}

HRESULT CFilterCoreFactory::GetExtraFilters(const CFileItem& pFileItem, std::vector<std::string>& filters, bool dxva /*= false*/)
{
  filters.clear();
  CGlobalFilterSelectionRule * pRule = GetGlobalFilterSelectionRule(pFileItem);
  if (!pRule)
    return E_FAIL;

  pRule->GetExtraFilters(pFileItem, filters, dxva);
  return S_OK;
}

HRESULT CFilterCoreFactory::GetShaders(const CFileItem& pFileItem, std::vector<uint32_t>& shaders, std::vector<uint32_t>& shadersStage, bool dxva /*= false*/)
{
  shaders.clear();
  CGlobalFilterSelectionRule * pRule = GetGlobalFilterSelectionRule(pFileItem);
  if (!pRule)
    return E_FAIL;

  pRule->GetShaders(pFileItem, shaders, shadersStage, dxva);
  return S_OK;
}

CFGFilter* CFilterCoreFactory::GetFilterFromName(const std::string& _filter, bool showError)
{

  std::string filter = _filter;

  // Right now we only have the rar source filter
  for (int i = 0; i < countof(internalFilters); i++)
  {
    if (StringUtils::EqualsNoCase(internalFilters[i].name, filter))
    {
      return internalFilters[i].cst(internalFilters[i].osdname);
    }
  }

  for (std::vector<CFGFilterFile*>::const_iterator it = m_Filters.begin(); it != m_Filters.end(); it++)
  {
    if (CompareCFGFilterFileToString(*it, filter))
    {
      CLog::Log(LOGINFO, "{} Filter \"{}\" found", __FUNCTION__, filter.c_str());
      return *it;
      
    }
  }


    CLog::Log(LOGERROR, "{} Filter \"{}\" isn't loaded. Please check dsfilterconfig.xml", __FUNCTION__, filter.c_str());
    if (showError)
    {
      CGUIDialogOK *dialog = (CGUIDialogOK *)CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_OK);
      if (dialog)
      {
        dialog->SetHeading("Filter not found");
        dialog->SetLine(0, "The filter \"" + filter + "\" isn't loaded.");
        dialog->SetLine(1, "Please check your dsfilterconfig.xml.");
        CDSPlayer::errorWindow = dialog;
      }
    }

    return NULL;


}
std::vector<CGlobalFilterSelectionRule *> CFilterCoreFactory::m_selecRules;
std::vector<CFGFilterFile *> CFilterCoreFactory::m_Filters;

#endif