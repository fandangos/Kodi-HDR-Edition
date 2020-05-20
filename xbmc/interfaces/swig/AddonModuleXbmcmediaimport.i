/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

%module xbmcmediaimport

%begin %{
#if defined(TARGET_WINDOWS) || defined(TARGET_WIN10)
#  if !defined(WIN32_LEAN_AND_MEAN)
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include "interfaces/legacy/ModuleXbmcmediaimport.h"
#include "interfaces/legacy/mediaimport/Exceptions.h"
#include "interfaces/legacy/mediaimport/MediaImport.h"
#include "interfaces/legacy/mediaimport/MediaProvider.h"
#include "interfaces/legacy/mediaimport/Observer.h"

using namespace XBMCAddon;
using namespace xbmcmediaimport;

#if defined(__GNUG__) && (__GNUC__>4) || (__GNUC__==4 && __GNUC_MINOR__>=2)
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

%}

// This is all about warning suppression. It's OK that these base classes are 
// not part of what swig parses.
%feature("knownbasetypes") XBMCAddon::xbmcaddon "AddonClass"
%feature("knownapitypes") XBMCAddon::xbmcmediaimport "XBMCAddon::xbmcaddon::Settings,XBMCAddon::xbmcgui::ListItem"

%feature("director") Observer;

%include "interfaces/legacy/swighelper.h"
%include "interfaces/legacy/AddonString.h"
%include "interfaces/legacy/ModuleXbmcmediaimport.h"
%include "interfaces/legacy/mediaimport/Exceptions.h"
%include "interfaces/legacy/mediaimport/MediaImport.h"
%include "interfaces/legacy/mediaimport/MediaProvider.h"
%include "interfaces/legacy/mediaimport/Observer.h"

