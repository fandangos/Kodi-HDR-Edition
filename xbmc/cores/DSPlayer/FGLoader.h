/*
 *      Copyright (C) 2005-2009 Team XBMC
 *      http://www.xbmc.org
 *
 *		Copyright (C) 2010-2013 Eduard Kytmanov
 *		http://www.avmedia.su
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

#pragma once

#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif
#include "GraphFilters.h"
#include "FgFilter.h"
#include "FileItem.h"
#include "SComCli.h"
#include "threads/CriticalSection.h"

enum ESettingsType
{
  MEDIAS,
  FILTERS
};

class CFGLoader : public CCriticalSection
{
public:
  CFGLoader();
  virtual ~CFGLoader();

  HRESULT    LoadConfig(FILTERSMAN_TYPE filterManager = NOFILTERMAN);
  bool	     IsAutoRender(){ return m_bIsAutoRender; };
  bool       LoadFilterCoreFactorySettings(const std::string& item, ESettingsType type, bool clear, int iPriority = 0);

  HRESULT    LoadFilterRules(const CFileItem& pFileItem);
  HRESULT    InsertSourceFilter(CFileItem& pFileItem, const std::string& filterName);
  HRESULT    InsertSplitter(const CFileItem& pFileItem, const std::string& filterName);
  HRESULT    InsertAudioRenderer(const std::string& filterName);
  HRESULT    InsertVideoRenderer();
  HRESULT    InsertFilter(const std::string& filterName, SFilterInfos& f);

  static void SettingOptionsDSVideoRendererFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current);
  static void SettingOptionsDSAudioRendererFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current);
  static void SettingOptionsSanearDevicesFiller(const std::shared_ptr<const CSetting>& setting, std::vector<StringSettingOption>& list, std::string& current);

protected:
  std::string                m_xbmcConfigFilePath;
  CFGFilterVideoRenderer*   m_pFGF;
  bool					    m_bIsAutoRender;

private:
  void      ParseStreamingType(CFileItem& pFileItem, IBaseFilter* pBF);
};

