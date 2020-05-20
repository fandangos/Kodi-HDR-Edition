/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "addons/IAddon.h"
#include "addons/settings/IAddonSettingsCallbackExecutor.h"
#include "commons/Exception.h"
#include "interfaces/legacy/AddonClass.h"
#include "interfaces/legacy/AddonString.h"
#include "interfaces/legacy/Exception.h"
#include "interfaces/legacy/Tuple.h"
#include "settings/SettingsBase.h"
#include "settings/lib/SettingDefinitions.h"

#include <vector>

namespace XBMCAddon
{
namespace xbmcaddon
{
XBMCCOMMONS_STANDARD_EXCEPTION(SettingCallbacksNotSupportedException);

//
/// \addtogroup python_xbmcaddon
/// @{
/// @brief **Add-on settings**
///
/// This wrapper provides access to the settings specific to an add-on.
/// It supports reading and writing specific setting values.
///
/// \python_class{ Settings() }
///
///
///-----------------------------------------------------------------------
/// @python_v19
///
/// **Example:**
/// ~~~~~~~~~~~~~{.py}
/// ...
/// settings = xbmc.Addon('id').getSettings()
/// ...
/// ~~~~~~~~~~~~~
class Settings : public AddonClass
#ifndef SWIG
  ,
                 protected ISettingCallback
#endif
{
public:
#if !defined SWIG && !defined DOXYGEN_SHOULD_SKIP_THIS
  std::shared_ptr<CSettingsBase> settings;
#endif

#ifndef SWIG
  inline Settings(std::shared_ptr<CSettingsBase> settings,
                  const std::string& addonId,
                  ADDON::IAddonSettingsCallbackExecutor* callbackExecutor = nullptr,
                  void* callbackData = nullptr)
    : settings(settings),
      m_addonId(addonId),
      m_callbackExecutor(callbackExecutor),
      m_callbackData(callbackData)
  {
  }
#endif

  virtual ~Settings() {}

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// TODO(Montellese)
  ///
  load(...);
#else
  bool load();
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// TODO(Montellese)
  ///
  save(...);
#else
  bool save();
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// TODO(Montellese)
  ///
  setLoaded(...);
#else
  void setLoaded();
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ getBool(id) }
  ///-----------------------------------------------------------------------
  /// Returns the value of a setting as a boolean.
  ///
  /// @param id                      string - id of the setting that the module
  ///                                needs to access.
  /// @return                        Setting as a boolean
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// enabled = settings.getBool('enabled')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  getBool(...);
#else
  bool getBool(const char* id) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ getInt(id) }
  ///-----------------------------------------------------------------------
  /// Returns the value of a setting as an integer.
  ///
  /// @param id                      string - id of the setting that the module
  ///                                needs to access.
  /// @return                        Setting as an integer
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// max = settings.getInt('max')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  getInt(...);
#else
  int getInt(const char* id) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ getNumber(id) }
  ///-----------------------------------------------------------------------
  /// Returns the value of a setting as a floating point number.
  ///
  /// @param id                      string - id of the setting that the module
  ///                                needs to access.
  /// @return                        Setting as a floating point number
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// max = settings.getNumber('max')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  getNumber(...);
#else
  double getNumber(const char* id) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ getString(id) }
  ///-----------------------------------------------------------------------
  /// Returns the value of a setting as a unicode string.
  ///
  /// @param id                      string - id of the setting that the module
  ///                                needs to access.
  /// @return                        Setting as a unicode string
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// apikey = settings.getString('apikey')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  getString(...);
#else
  String getString(const char* id) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ getBoolList(id) }
  ///-----------------------------------------------------------------------
  /// Returns the value of a setting as a list of booleans.
  ///
  /// @param id                      string - id of the setting that the module
  ///                                needs to access.
  /// @return                        Setting as a list of booleans
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// enabled = settings.getBoolList('enabled')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  getBoolList(...);
#else
  std::vector<bool> getBoolList(const char* id) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ getIntList(id) }
  ///-----------------------------------------------------------------------
  /// Returns the value of a setting as a list of integers.
  ///
  /// @param id                      string - id of the setting that the module
  ///                                needs to access.
  /// @return                        Setting as a list of integers
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// ids = settings.getIntList('ids')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  getIntList(...);
#else
  std::vector<int> getIntList(const char* id) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ getNumberList(id) }
  ///-----------------------------------------------------------------------
  /// Returns the value of a setting as a list of floating point numbers.
  ///
  /// @param id                      string - id of the setting that the module
  ///                                needs to access.
  /// @return                        Setting as a list of floating point numbers
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// max = settings.getNumberList('max')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  getNumberList(...);
#else
  std::vector<double> getNumberList(const char* id) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ getStringList(id) }
  ///-----------------------------------------------------------------------
  /// Returns the value of a setting as a list of unicode strings.
  ///
  /// @param id                      string - id of the setting that the module
  ///                                needs to access.
  /// @return                        Setting as a list of unicode strings
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// views = settings.getStringList('views')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  getStringList(...);
#else
  std::vector<String> getStringList(const char* id) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ setBool(id, value) }
  ///-----------------------------------------------------------------------
  /// Sets the value of a setting.
  ///
  /// @param id                  string - id of the setting that the module needs to access.
  /// @param value               boolean - value of the setting.
  /// @return                    True if the value of the setting was set, false otherwise
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.setBool(id='enabled', value=True)
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  setBool(...);
#else
  void setBool(const char* id, bool value) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ setInt(id, value) }
  ///-----------------------------------------------------------------------
  /// Sets the value of a setting.
  ///
  /// @param id                  string - id of the setting that the module needs to access.
  /// @param value               integer - value of the setting.
  /// @return                    True if the value of the setting was set, false otherwise
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.setInt(id='max', value=5)
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  setInt(...);
#else
  void setInt(const char* id, int value) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ setNumber(id, value) }
  ///-----------------------------------------------------------------------
  /// Sets the value of a setting.
  ///
  /// @param id                  string - id of the setting that the module needs to access.
  /// @param value               float - value of the setting.
  /// @return                    True if the value of the setting was set, false otherwise
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.setNumber(id='max', value=5.5)
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  setNumber(...);
#else
  void setNumber(const char* id, double value) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ setString(id, value) }
  ///-----------------------------------------------------------------------
  /// Sets the value of a setting.
  ///
  /// @param id                  string - id of the setting that the module needs to access.
  /// @param value               string or unicode - value of the setting.
  /// @return                    True if the value of the setting was set, false otherwise
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.setString(id='username', value='teamkodi')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  setString(...);
#else
  void setString(const char* id, const String& value) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ setBoolList(id, values) }
  ///-----------------------------------------------------------------------
  /// Sets the boolean values of a list setting.
  ///
  /// @param id                  string - id of the setting that the module needs to access.
  /// @param values              list of boolean - values of the setting.
  /// @return                    True if the values of the setting were set, false otherwise
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.setBoolList(id='enabled', values=[ True, False ])
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  setBoolList(...);
#else
  void setBoolList(const char* id,
                   const std::vector<bool>& values) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ setIntList(id, value) }
  ///-----------------------------------------------------------------------
  /// Sets the integer values of a list setting.
  ///
  /// @param id                  string - id of the setting that the module needs to access.
  /// @param values              list of int - values of the setting.
  /// @return                    True if the values of the setting were set, false otherwise
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.setIntList(id='max', values=[ 5, 23 ])
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  setIntList(...);
#else
  void setIntList(const char* id,
                  const std::vector<int>& values) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ setNumberList(id, value) }
  ///-----------------------------------------------------------------------
  /// Sets the floating point values of a list setting.
  ///
  /// @param id                  string - id of the setting that the module needs to access.
  /// @param values              list of float - values of the setting.
  /// @return                    True if the values of the setting were set, false otherwise
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.setNumberList(id='max', values=[ 5.5, 5.8 ])
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  setNumberList(...);
#else
  void setNumberList(const char* id,
                     const std::vector<double>& values) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ setStringList(id, value) }
  ///-----------------------------------------------------------------------
  /// Sets the string values of a list setting.
  ///
  /// @param id                  string - id of the setting that the module needs to access.
  /// @param values              list of string or unicode - values of the setting.
  /// @return                    True if the values of the setting were set, false otherwise
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.setStringList(id='username', values=[ 'team', 'kodi' ])
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  setStringList(...);
#else
  void setStringList(const char* id,
                     const std::vector<String>& values) throw(XBMCAddon::WrongTypeException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// \ingroup python_xbmcaddon
  /// @brief \python_func{ registerActionCallback(settingId, callback) }
  ///-----------------------------------------------------------------------
  /// TODO
  ///
  /// @param settingId           string - TODO.
  /// @param callback            string - TODO.
  /// @return                    TODO
  ///
  ///
  /// @note You can use the above as keywords for arguments.
  ///
  ///
  ///-----------------------------------------------------------------------
  /// @python_v19
  ///
  /// **Example:**
  /// ~~~~~~~~~~~~~{.py}
  /// ..
  /// settings.registerActionCallback('foo', 'onSettingAction')
  /// ..
  /// ~~~~~~~~~~~~~
  ///
  registerActionCallback(...);
#else
  bool registerActionCallback(const String& settingId,
                              const String& callback) throw(SettingCallbacksNotSupportedException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// TODO(Montellese)
  ///
  registerOptionsFillerCallback(...);
#else
  bool registerOptionsFillerCallback(const String& settingId, const String& callback) throw(
      SettingCallbacksNotSupportedException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// TODO(Montellese)
  ///
  setIntegerOptions(...);
#else
  bool setIntegerOptions(
      const String& settingId,
      const std::vector<Tuple<String, int>>& options) throw(SettingCallbacksNotSupportedException);
#endif

#ifdef DOXYGEN_SHOULD_USE_THIS
  ///
  /// TODO(Montellese)
  ///
  setStringOptions(...);
#else
  bool setStringOptions(const String& settingId,
                        const std::vector<Tuple<String, String>>&
                            options) throw(SettingCallbacksNotSupportedException);
#endif

#ifndef SWIG
protected:
  void OnSettingAction(std::shared_ptr<const CSetting> setting) override;

private:
  using CallbackMap = std::map<std::string, std::string>;

  std::shared_ptr<CSetting> GetSetting(const std::string& settingId) const;

  static void IntegerSettingOptionsFiller(std::shared_ptr<const CSetting> setting,
                                          IntegerSettingOptions& list,
                                          int& current,
                                          void* data);
  static void StringSettingOptionsFiller(std::shared_ptr<const CSetting> setting,
                                         StringSettingOptions& list,
                                         std::string& current,
                                         void* data);

  const std::string m_addonId;

  ADDON::IAddonSettingsCallbackExecutor* m_callbackExecutor;
  void* m_callbackData;
  CallbackMap m_actionCallbacks;
  CallbackMap m_optionsFillerCallbacks;
#endif
};
} // namespace xbmcaddon
} // namespace XBMCAddon
