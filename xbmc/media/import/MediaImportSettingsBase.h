/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "settings/SettingControl.h"
#include "settings/SettingsBase.h"
#include "settings/lib/SettingConditions.h"
#include "settings/lib/SettingDefinitions.h"
#include "utils/StaticLoggerBase.h"

#include <set>
#include <string>
#include <tuple>

class CSettingSection;
class CSettingsManager;

class CMediaImportSettingsBase : public CSettingsBase,
                                 public CSettingControlCreator,
                                 protected CStaticLoggerBase
{
public:
  explicit CMediaImportSettingsBase(const std::string& settingValues = "");
  CMediaImportSettingsBase(const CMediaImportSettingsBase& other);
  virtual ~CMediaImportSettingsBase() = default;

  CMediaImportSettingsBase& operator=(const CMediaImportSettingsBase& other);

  bool operator==(const CMediaImportSettingsBase& other) const;
  bool operator!=(const CMediaImportSettingsBase& other) const { return !(*this == other); }

  // implementations of CSettingsBase
  bool Load() override;
  bool Save() override;

  // specialization of CSettingsBase
  void Unload() override;

  bool HasDefinition(const std::string& settingDefinition) const;
  void AddDefinition(const std::string& settingDefinition);

  void AddSimpleCondition(const std::string& condition);
  void AddComplexCondition(const std::string& name,
                           const SettingConditionCheck& condition,
                           void* data = nullptr);

  void SetOptionsFiller(const std::string& settingId,
                        IntegerSettingOptionsFiller optionsFiller,
                        void* data = nullptr);
  void SetOptionsFiller(const std::string& settingId,
                        StringSettingOptionsFiller optionsFiller,
                        void* data = nullptr);

  std::string ToXml() const;

protected:
  // implementation of CSettingsBase
  bool InitializeDefinitions() override;

  // specializations of CSettingsBase
  void InitializeControls() override;
  void InitializeConditions() override;

  // hide methods of CSettingsBase
  using CSettingsBase::Initialize;
  using CSettingsBase::SetLoaded;
  using CSettingsBase::Uninitialize;

private:
  mutable std::string m_settingValues;
  std::set<std::string> m_settingDefinitions;

  std::set<std::string> m_simpleConditions;
  std::map<std::string, std::tuple<SettingConditionCheck, void*>> m_complexConditions;
};

using MediaImportSettingsBasePtr = std::shared_ptr<CMediaImportSettingsBase>;
