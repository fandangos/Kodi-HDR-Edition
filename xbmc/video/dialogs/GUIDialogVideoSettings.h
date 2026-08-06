/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Interface/StreamInfo.h"
#include "settings/dialogs/GUIDialogSettingsManualBase.h"

#include <string>
#include <utility>
#include <vector>


#if HAS_DS_PLAYER
#include "cores/DSPlayer/Dialogs/GUIDialogMadvrSettingsBase.h"
#include "DSPropertyPage.h"
class CGUIDialogVideoSettings : public CGUIDialogMadvrSettingsBase
#else
class CGUIDialogVideoSettings : public CGUIDialogSettingsManualBase
#endif

//struct IntegerSettingOption;

//class CGUIDialogVideoSettings : public CGUIDialogSettingsManualBase
{
public:
  CGUIDialogVideoSettings();
  ~CGUIDialogVideoSettings() override;

protected:
  // implementations of ISettingCallback
  void OnSettingChanged(const std::shared_ptr<const CSetting>& setting) override;
  void OnSettingAction(const std::shared_ptr<const CSetting>& setting) override;

  void AddVideoStreams(const std::shared_ptr<CSettingGroup>& group, const std::string& settingId);
  static void VideoStreamsOptionFiller(const std::shared_ptr<const CSetting>& setting,
                                       std::vector<IntegerSettingOption>& list,
                                       int& current);

  static void VideoOrientationFiller(const std::shared_ptr<const CSetting>& setting,
                                     std::vector<IntegerSettingOption>& list,
                                     int& current);

  static std::string FormatFlags(StreamFlags flags);

  // specialization of CGUIDialogSettingsBase
  bool AllowResettingSettings() const override { return false; }
  bool Save() override;
  void SetupView() override;

  // specialization of CGUIDialogSettingsManualBase
  void InitializeSettings() override;

private:
  int m_videoStream;
  bool m_viewModeChanged = false;
#if HAS_DS_PLAYER
  CDSPropertyPage* m_pDSPropertyPage;
  int m_scalingMethod;
  int m_dsStats;
  int m_placeboOption;
  int m_pPlaneMerger;
#endif
};
