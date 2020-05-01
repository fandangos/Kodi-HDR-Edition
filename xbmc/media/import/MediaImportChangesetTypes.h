/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "FileItem.h"

enum class MediaImportChangesetType
{
  None = 0,
  Added,
  Changed,
  Removed
};

typedef std::pair<MediaImportChangesetType, CFileItemPtr> ChangesetItemPtr;
typedef std::vector<ChangesetItemPtr> ChangesetItems;
