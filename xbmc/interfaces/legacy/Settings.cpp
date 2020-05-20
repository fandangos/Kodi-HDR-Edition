/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Settings.h"

#include "settings/lib/Setting.h"

#include <algorithm>
#include <functional>

namespace XBMCAddon
{
namespace xbmcaddon
{
template<class TSetting>
bool GetSettingValue(std::shared_ptr<CSettingsBase> settings,
                     const std::string& key,
                     typename TSetting::Value& value)
{
  if (key.empty() || !settings->IsLoaded())
    return false;

  auto setting = settings->GetSetting(key);
  if (setting == nullptr || setting->GetType() != TSetting::Type())
    return false;

  value = std::static_pointer_cast<TSetting>(setting)->GetValue();
  return true;
}

template<class TSetting>
bool GetSettingValueList(std::shared_ptr<CSettingsBase> settings,
                         const std::string& key,
                         std::function<typename TSetting::Value(CVariant)> transform,
                         std::vector<typename TSetting::Value>& values)
{
  if (key.empty() || !settings->IsLoaded())
    return false;

  auto setting = settings->GetSetting(key);
  if (setting == nullptr || setting->GetType() != SettingType::List ||
      std::static_pointer_cast<CSettingList>(setting)->GetElementType() != TSetting::Type())
    return false;

  const auto variantValues = settings->GetList(key);
  std::transform(variantValues.begin(), variantValues.end(), std::back_inserter(values), transform);
  return true;
}

template<class TSetting>
bool SetSettingValue(std::shared_ptr<CSettingsBase> settings,
                     const std::string& key,
                     typename TSetting::Value value)
{
  if (key.empty() || !settings->IsLoaded())
    return false;

  // try to get the setting
  auto setting = settings->GetSetting(key);
  if (setting == nullptr || setting->GetType() != TSetting::Type())
    return false;

  return std::static_pointer_cast<TSetting>(setting)->SetValue(value);
}

template<class TSetting>
bool SetSettingValueList(std::shared_ptr<CSettingsBase> settings,
                         const std::string& key,
                         const std::vector<typename TSetting::Value>& values)
{
  if (key.empty() || !settings->IsLoaded())
    return false;

  // try to get the setting
  auto setting = settings->GetSetting(key);
  if (setting == nullptr || setting->GetType() != SettingType::List ||
      std::static_pointer_cast<CSettingList>(setting)->GetElementType() != TSetting::Type())
    return false;

  std::vector<CVariant> variantValues;
  std::transform(values.begin(), values.end(), std::back_inserter(variantValues),
                 [](typename TSetting::Value value) { return CVariant(value); });

  return settings->SetList(key, variantValues);
}

bool Settings::load()
{
  if (m_callbackExecutor != nullptr)
    return m_callbackExecutor->LoadSettings(settings, m_callbackData);

  return settings->IsLoaded() || settings->Load();
}

bool Settings::save()
{
  if (m_callbackExecutor != nullptr)
    return m_callbackExecutor->SaveSettings(settings, m_callbackData);

  return settings->Save();
}

void Settings::setLoaded()
{
  if (m_callbackExecutor == nullptr)
    return;

  m_callbackExecutor->SetSettingsLoaded(m_callbackData);
}

bool Settings::getBool(const char* id) throw(XBMCAddon::WrongTypeException)
{
  bool value = false;
  if (!GetSettingValue<CSettingBool>(settings, id, value))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"boolean\" for \"%s\"", id);

  return value;
}

int Settings::getInt(const char* id) throw(XBMCAddon::WrongTypeException)
{
  int value = 0;
  if (!GetSettingValue<CSettingInt>(settings, id, value))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"integer\" for \"%s\"", id);

  return value;
}

double Settings::getNumber(const char* id) throw(XBMCAddon::WrongTypeException)
{
  double value = 0.0;
  if (!GetSettingValue<CSettingNumber>(settings, id, value))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"number\" for \"%s\"", id);

  return value;
}

String Settings::getString(const char* id) throw(XBMCAddon::WrongTypeException)
{
  std::string value;
  if (!GetSettingValue<CSettingString>(settings, id, value))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"string\" for \"%s\"", id);

  return value;
}

std::vector<bool> Settings::getBoolList(const char* id) throw(XBMCAddon::WrongTypeException)
{
  const auto transform = [](CVariant value) { return value.asBoolean(); };
  std::vector<bool> values;
  if (!GetSettingValueList<CSettingBool>(settings, id, transform, values))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"list[boolean]\" for \"%s\"", id);

  return values;
}

std::vector<int> Settings::getIntList(const char* id) throw(XBMCAddon::WrongTypeException)
{
  const auto transform = [](CVariant value) { return value.asInteger32(); };
  std::vector<int> values;
  if (!GetSettingValueList<CSettingInt>(settings, id, transform, values))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"list[integer]\" for \"%s\"", id);

  return values;
}

std::vector<double> Settings::getNumberList(const char* id) throw(XBMCAddon::WrongTypeException)
{
  const auto transform = [](CVariant value) { return value.asDouble(); };
  std::vector<double> values;
  if (!GetSettingValueList<CSettingNumber>(settings, id, transform, values))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"list[number]\" for \"%s\"", id);

  return values;
}

std::vector<String> Settings::getStringList(const char* id) throw(XBMCAddon::WrongTypeException)
{
  const auto transform = [](CVariant value) { return value.asString(); };
  std::vector<std::string> values;
  if (!GetSettingValueList<CSettingString>(settings, id, transform, values))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"list[string]\" for \"%s\"", id);

  return values;
}

void Settings::setBool(const char* id, bool value) throw(XBMCAddon::WrongTypeException)
{
  if (!SetSettingValue<CSettingBool>(settings, id, value))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"boolean\" for \"%s\"", id);
  settings->Save();
}

void Settings::setInt(const char* id, int value) throw(XBMCAddon::WrongTypeException)
{
  if (!SetSettingValue<CSettingInt>(settings, id, value))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"integer\" for \"%s\"", id);
  settings->Save();
}

void Settings::setNumber(const char* id, double value) throw(XBMCAddon::WrongTypeException)
{
  if (!SetSettingValue<CSettingNumber>(settings, id, value))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"number\" for \"%s\"", id);
  settings->Save();
}

void Settings::setString(const char* id, const String& value) throw(XBMCAddon::WrongTypeException)
{
  if (!SetSettingValue<CSettingString>(settings, id, value))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"string\" for \"%s\"", id);
  settings->Save();
}

void Settings::setBoolList(const char* id,
                           const std::vector<bool>& values) throw(XBMCAddon::WrongTypeException)
{
  if (!SetSettingValueList<CSettingBool>(settings, id, values))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"list[boolean]\" for \"%s\"", id);
  settings->Save();
}

void Settings::setIntList(const char* id,
                          const std::vector<int>& values) throw(XBMCAddon::WrongTypeException)
{
  if (!SetSettingValueList<CSettingInt>(settings, id, values))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"list[integer]\" for \"%s\"", id);
  settings->Save();
}

void Settings::setNumberList(const char* id,
                             const std::vector<double>& values) throw(XBMCAddon::WrongTypeException)
{
  if (!SetSettingValueList<CSettingNumber>(settings, id, values))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"list[number]\" for \"%s\"", id);
  settings->Save();
}

void Settings::setStringList(const char* id,
                             const std::vector<String>& values) throw(XBMCAddon::WrongTypeException)
{
  if (!SetSettingValueList<CSettingString>(settings, id, values))
    throw XBMCAddon::WrongTypeException("Invalid setting type \"list[string]\" for \"%s\"", id);
  settings->Save();
}

bool Settings::registerActionCallback(const String& settingId, const String& callback) throw(
    SettingCallbacksNotSupportedException)
{
  if (m_callbackExecutor == nullptr)
    throw SettingCallbacksNotSupportedException("Add-on %s does not support setting callbacks",
                                                m_addonId.c_str());

  if (settingId.empty() || !settings->IsLoaded())
    return false;

  auto setting = settings->GetSetting(settingId);
  if (setting == nullptr || setting->GetType() != SettingType::Action)
    return false;

  settings->RegisterCallback(this, {settingId});
  m_actionCallbacks[settingId] = callback;
  return true;
}

bool Settings::registerOptionsFillerCallback(const String& settingId, const String& callback) throw(
    SettingCallbacksNotSupportedException)
{
  if (m_callbackExecutor == nullptr)
    throw SettingCallbacksNotSupportedException("Add-on %s does not support setting callbacks",
                                                m_addonId.c_str());

  if (settingId.empty() || !settings->IsLoaded())
    return false;

  auto setting = GetSetting(settingId);
  if (setting == nullptr)
    return false;

  // make sure the setting is either an integer or a string or a list of one of the two
  const SettingType settingType = setting->GetType();
  if (settingType != SettingType::Integer && settingType != SettingType::String)
    return false;

  // set setting options filler and register our wrapper filler
  // ATTENTION: we must use CSetting::GetId() to get the settings proper
  // identifier because GetSetting() also handles CSettingList settings
  m_optionsFillerCallbacks[setting->GetId()] = callback;
  switch (settingType)
  {
    case SettingType::Integer:
      std::static_pointer_cast<CSettingInt>(setting)->SetOptionsFiller(IntegerSettingOptionsFiller,
                                                                       this);
      break;

    case SettingType::String:
      std::static_pointer_cast<CSettingString>(setting)->SetOptionsFiller(
          StringSettingOptionsFiller, this);
      break;

    default:
      return false;
  }

  return true;
}

bool Settings::setIntegerOptions(
    const String& settingId,
    const std::vector<Tuple<String, int>>& options) throw(SettingCallbacksNotSupportedException)
{
  if (m_callbackExecutor == nullptr)
    throw SettingCallbacksNotSupportedException("Add-on %s does not support setting callbacks",
                                                m_addonId.c_str());

  if (settingId.empty() || !settings->IsLoaded())
    return false;

  auto setting = GetSetting(settingId);
  if (setting == nullptr)
    return false;

  // make sure the setting is an integer or a list of integers
  if (setting->GetType() != SettingType::Integer)
    return false;

  // convert options
  IntegerSettingOptions integerOptions;
  for (const auto& option : options)
    integerOptions.emplace_back(option.first(), option.second());

  return m_callbackExecutor->SetIntegerSettingOptions(settingId, integerOptions, m_callbackData);
}

bool Settings::setStringOptions(
    const String& settingId,
    const std::vector<Tuple<String, String>>& options) throw(SettingCallbacksNotSupportedException)
{
  if (m_callbackExecutor == nullptr)
    throw SettingCallbacksNotSupportedException("Add-on %s does not support setting callbacks",
                                                m_addonId.c_str());

  if (settingId.empty() || !settings->IsLoaded())
    return false;

  auto setting = GetSetting(settingId);
  if (setting == nullptr)
    return false;

  // make sure the setting is a string or a list of strings
  if (setting->GetType() != SettingType::String)
    return false;

  // convert options
  StringSettingOptions stringOptions;
  for (const auto& option : options)
    stringOptions.emplace_back(option.first(), option.second());

  return m_callbackExecutor->SetStringSettingOptions(settingId, stringOptions, m_callbackData);
}

#ifndef SWIG
void Settings::OnSettingAction(std::shared_ptr<const CSetting> setting)
{
  if (m_callbackExecutor == nullptr || setting == nullptr)
    return;

  const auto& callback = m_actionCallbacks.find(setting->GetId());
  if (callback == m_actionCallbacks.end())
    return;

  m_callbackExecutor->OnSettingAction(setting, callback->second, m_callbackData);
}

std::shared_ptr<CSetting> Settings::GetSetting(const std::string& settingId) const
{
  if (settingId.empty() || !settings->IsLoaded())
    return nullptr;

  auto setting = settings->GetSetting(settingId);
  if (setting == nullptr)
    return nullptr;

  if (setting->GetType() == SettingType::List)
    setting = std::static_pointer_cast<CSettingList>(setting)->GetDefinition();

  return setting;
}

void Settings::IntegerSettingOptionsFiller(std::shared_ptr<const CSetting> setting,
                                           IntegerSettingOptions& list,
                                           int& current,
                                           void* data)
{
  if (setting == nullptr || data == nullptr)
    return;

  auto settings = static_cast<Settings*>(data);
  if (settings->m_callbackExecutor == nullptr)
    return;

  const auto& callback = settings->m_optionsFillerCallbacks.find(setting->GetId());
  if (callback == settings->m_optionsFillerCallbacks.end())
    return;

  settings->m_callbackExecutor->OnIntegerSettingOptionsFiller(setting, callback->second, list,
                                                              current, settings->m_callbackData);
}

void Settings::StringSettingOptionsFiller(std::shared_ptr<const CSetting> setting,
                                          StringSettingOptions& list,
                                          std::string& current,
                                          void* data)
{
  if (setting == nullptr || data == nullptr)
    return;

  auto settings = static_cast<Settings*>(data);
  if (settings->m_callbackExecutor == nullptr)
    return;

  const auto& callback = settings->m_optionsFillerCallbacks.find(setting->GetId());
  if (callback == settings->m_optionsFillerCallbacks.end())
    return;

  settings->m_callbackExecutor->OnStringSettingOptionsFiller(setting, callback->second, list,
                                                             current, settings->m_callbackData);
}
#endif
} // namespace xbmcaddon
} // namespace XBMCAddon
