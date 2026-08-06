#pragma once

/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://xbmc.org
 *
 *      Copyright (C) 2014-2015 Aracnoz
 *      http://github.com/aracnoz/xbmc
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "settings/dialogs/GUIDialogSettingsManualBase.h"
#include "GUIDialogDSManager.h"

class CGUIDialogDSFilters : public CGUIDialogSettingsManualBase
{
public:
  CGUIDialogDSFilters();
  virtual ~CGUIDialogDSFilters();

  static void ShowDSFiltersList();

protected:

  // implementations of ISettingCallback
  virtual void OnSettingChanged(const std::shared_ptr<const CSetting>& setting);
  virtual void OnSettingAction(const std::shared_ptr<const CSetting>& setting);
  virtual void OnInitWindow();
  virtual void OnDeinitWindow(int nextWindowID);

  // specialization of CGUIDialogSettingsBase
  virtual bool AllowResettingSettings() const { return false; }
  virtual bool Save();

  // specialization of CGUIDialogSettingsManualBase
  virtual void InitializeSettings();

  virtual void SetupView();

  static void TypeOptionFiller(const std::shared_ptr<const CSetting>& setting, StringSettingOptions& list, std::string& current);
  std::string GetFilterName(std::string guid);

  bool m_bEdited;

  std::vector<DSConfigList *> m_filterList;
  CGUIDialogDSManager* m_dsmanager;

private:
  void ActionInternal(const std::string &settingId);
};
