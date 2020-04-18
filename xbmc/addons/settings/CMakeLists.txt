set(SOURCES AddonSettings.cpp
            GUIDialogAddonSettings.cpp
            SettingUrlEncodedString.cpp)

set(HEADERS AddonSettings.h
            GUIDialogAddonSettings.h
            IAddonSettingsCallbackExecutor.h
            SettingUrlEncodedString.h)

core_add_library(addons_settings)
