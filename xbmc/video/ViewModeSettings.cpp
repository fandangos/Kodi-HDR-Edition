/*
 *  Copyright (C) 2016-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ViewModeSettings.h"

#include "ServiceBroker.h"
#include "cores/VideoSettings.h"
#include "resources/LocalizeStrings.h"
#include "resources/ResourcesComponent.h"
#include "settings/lib/SettingDefinitions.h"

struct ViewModeProperties
{
  int stringIndex;
  int viewMode;
  bool hideFromQuickCycle = false;
  bool hideFromList = false;
};

#define HIDE_ITEM true

/** The list of all the view modes along with their properties
 */
static const ViewModeProperties viewModes[] =
{
  { 630,   ViewModeNormal },
  { 631,   ViewModeZoom },
  { 39008, ViewModeZoom120Width },
  { 39009, ViewModeZoom110Width },
  { 632,   ViewModeStretch4x3 },
  { 633,   ViewModeWideZoom },
  { 634,   ViewModeStretch16x9 },
  { 644,   ViewModeStretch16x9Nonlin, HIDE_ITEM, HIDE_ITEM },
  { 635,   ViewModeOriginal },
  { 636,   ViewModeCustom, HIDE_ITEM }
};

#define NUMBER_OF_VIEW_MODES (sizeof(viewModes) / sizeof(viewModes[0]))

/** The list of all the view modes along with their properties
 "autoDetect|touchInside|touchOutside|stretch|100%|10%|20%|25%|30%|33%|40%|50%|60%|66%|70%|75%|80%|90%|110%|120%|125%|130%|140%|150%|160%|170%|175%|180%|190%|200%|225%|250%|300%|350%|400%|450%|500%|600%|700%|800%"¨*/

//55313
static const ViewModeProperties viewModesMadvr[] =
{
  { 55313 , ViewModeautoDetect },
{ 55314 , ViewModetouchInside },
{ 55315 , ViewModetouchOutside },
{ 55316 , ViewModestretch },
{ 55317 , ViewMode100 },
{ 55318 , ViewMode10 , HIDE_ITEM},
{ 55319 , ViewMode20 , HIDE_ITEM},
{ 55320 , ViewMode25 , HIDE_ITEM},
{ 55321 , ViewMode30 , HIDE_ITEM},
{ 55322 , ViewMode33 , HIDE_ITEM},
{ 55323 , ViewMode40 , HIDE_ITEM},
{ 55324 , ViewMode50 },
{ 55325 , ViewMode60 , HIDE_ITEM},
{ 55326 , ViewMode66 , HIDE_ITEM},
{ 55327 , ViewMode70 , HIDE_ITEM},
{ 55328 , ViewMode75 , HIDE_ITEM},
{ 55329 , ViewMode80 , HIDE_ITEM},
{ 55330 , ViewMode90 , HIDE_ITEM},
{ 55331 , ViewMode110 , HIDE_ITEM},
{ 55332 , ViewMode120 },
{ 55333 , ViewMode125 , HIDE_ITEM},
{ 55334 , ViewMode130 , HIDE_ITEM},
{ 55335 , ViewMode140 , HIDE_ITEM},
{ 55336 , ViewMode150 },
{ 55337 , ViewMode160 , HIDE_ITEM},
{ 55338 , ViewMode170 , HIDE_ITEM},
{ 55339 , ViewMode175 , HIDE_ITEM},
{ 55340 , ViewMode180 , HIDE_ITEM},
{ 55341 , ViewMode190 , HIDE_ITEM},
{ 55342 , ViewMode200 , HIDE_ITEM},
{ 55343 , ViewMode225 , HIDE_ITEM},
{ 55344 , ViewMode250 , HIDE_ITEM},
{ 55345 , ViewMode300 , HIDE_ITEM},
{ 55346 , ViewMode350 , HIDE_ITEM},
{ 55347 , ViewMode400 , HIDE_ITEM},
{ 55348 , ViewMode450 , HIDE_ITEM},
{ 55349 , ViewMode500 , HIDE_ITEM},
{ 55350 , ViewMode600 , HIDE_ITEM},
{ 55351 , ViewMode700 , HIDE_ITEM},
{ 55352 , ViewMode800 , HIDE_ITEM}
};

#define NUMBER_OF_VIEW_MODES_MADVR (sizeof(viewModesMadvr) / sizeof(viewModesMadvr[0]))

/** Gets the index of a view mode
 *
 * @param viewMode The view mode
 * @return The index of the view mode in the viewModes array
 */
static int GetViewModeIndex(int viewMode)
{
  size_t i;

  // Find the current view mode
  for (i = 0; i < NUMBER_OF_VIEW_MODES; i++)
  {
    if (viewModes[i].viewMode == viewMode)
      return i;
  }

  return 0; // An invalid view mode will always return the first view mode
}

/** Gets the index of a view mode
 *
 * @param viewMode The view mode
 * @return The index of the view mode in the viewModes array
 */
static int GetViewModeIndexMadvr(int viewMode)
{
  size_t i;

  // Find the current view mode
  for (i = 0; i < NUMBER_OF_VIEW_MODES_MADVR; i++)
  {
    if (viewModesMadvr[i].viewMode == viewMode)
      return i;
  }

  return 0; // An invalid view mode will always return the first view mode
}

/** Gets the next view mode for quick cycling through the modes
 *
 * @param viewMode The current view mode
 * @return The next view mode
 */
int CViewModeSettings::GetNextQuickCycleViewMode(int viewMode,bool madvr)
{
  if (madvr)
  {
    for (size_t i = GetViewModeIndexMadvr(viewMode) + 1; i < NUMBER_OF_VIEW_MODES_MADVR; i++)
    {
      if (!viewModesMadvr[i].hideFromQuickCycle)
        return viewModesMadvr[i].viewMode;
    }
    return ViewModeNormal;
  }
  // Find the next quick cycle view mode
  for (size_t i = GetViewModeIndex(viewMode) + 1; i < NUMBER_OF_VIEW_MODES; i++)
  {
    if (!viewModes[i].hideFromQuickCycle)
      return viewModes[i].viewMode;
  }

  return ViewModeNormal;
}

/** Gets the string index for the view mode
 *
 * @param viewMode The current view mode
 * @return The string index
 */
int CViewModeSettings::GetViewModeStringIndex(int viewMode,bool madvr)
{
  if (madvr)
    return viewModesMadvr[GetViewModeIndexMadvr(viewMode)].stringIndex;
  else
    return viewModes[GetViewModeIndex(viewMode)].stringIndex;
  
}

/** Fills the list with all visible view modes
 */
void CViewModeSettings::ViewModesFiller(const std::shared_ptr<const CSetting>& setting,
                                        std::vector<IntegerSettingOption>& list,
                                        int& current)
{
  // Add all appropriate view modes to the list control
  for (const auto &item : viewModes)
  {
    if (!item.hideFromList)
      list.emplace_back(
          CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(item.stringIndex),
          item.viewMode);
  }
}
