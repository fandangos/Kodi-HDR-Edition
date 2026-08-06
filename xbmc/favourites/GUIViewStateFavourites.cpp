/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIViewStateFavourites.h"

#include "FileItemList.h"
#include "guilib/WindowIDs.h"

CGUIViewStateFavourites::CGUIViewStateFavourites(const CFileItemList& items) : CGUIViewState(items)
{
  AddSortMethod(SortBy::USER_PREFERENCE, 19349,
                LABEL_MASKS("%L", "", "%L", "")); // Label, empty | Label, empty
  AddSortMethod(SortBy::LABEL, 551, LABEL_MASKS("%L", "", "%L", "")); // Label, empty | Label, empty

  SetSortMethod(SortBy::USER_PREFERENCE);

  LoadViewState(items.GetPath(), WINDOW_FAVOURITES);
}

void CGUIViewStateFavourites::SaveViewState()
{
  SaveViewToDb(m_items.GetPath(), WINDOW_FAVOURITES);
}
