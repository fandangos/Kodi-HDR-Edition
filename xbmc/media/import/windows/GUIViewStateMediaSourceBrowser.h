/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "view/GUIViewState.h"

class CGUIViewStateMediaSourceBrowser : public CGUIViewState
{
public:
  CGUIViewStateMediaSourceBrowser(const CFileItemList& items);
  ~CGUIViewStateMediaSourceBrowser() = default;

protected:
  // implementation of CGUIViewState
  virtual void SaveViewState();
  virtual std::string GetExtensions();
  virtual VECSOURCES& GetSources();
};
