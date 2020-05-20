/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "BaseMediaImporter.h"

#include "settings/lib/Setting.h"

bool CBaseMediaImporter::LoadSourceSettings(CMediaImportSource& source)
{
  return LoadSettings(source.Settings());
}

bool CBaseMediaImporter::UnloadSourceSettings(CMediaImportSource& source)
{
  return UnloadSettings(source.Settings());
}

bool CBaseMediaImporter::LoadImportSettings(CMediaImport& import)
{
  // first try to load the source's settings
  if (!LoadSourceSettings(import.GetSource()))
    return false;

  auto settings = import.Settings();
  if (!LoadSettings(settings))
    return false;

  auto settingUpdatePlaybackMetadataOnSource =
      settings->GetSetting(CMediaImportSettings::SettingUpdatePlaybackMetadataOnSource);
  if (settingUpdatePlaybackMetadataOnSource != nullptr)
  {
    settingUpdatePlaybackMetadataOnSource->SetEnabled(
        CanUpdatePlaycountOnSource(import.GetPath()) ||
        CanUpdateLastPlayedOnSource(import.GetPath()) ||
        CanUpdateResumePositionOnSource(import.GetPath()));
  }

  return true;
}

bool CBaseMediaImporter::UnloadImportSettings(CMediaImport& import)
{
  bool result = UnloadSettings(import.Settings());

  // also unload the source's settings
  if (!UnloadSourceSettings(import.GetSource()))
    return false;

  return result;
}

bool CBaseMediaImporter::LoadSettings(MediaImportSettingsBasePtr settings) const
{
  return settings->Load();
}

bool CBaseMediaImporter::UnloadSettings(MediaImportSettingsBasePtr settings) const
{
  if (!settings->IsLoaded())
    return true;

  // save the settings
  bool result = settings->Save();

  // unload the settings completely
  settings->Unload();

  return result;
}