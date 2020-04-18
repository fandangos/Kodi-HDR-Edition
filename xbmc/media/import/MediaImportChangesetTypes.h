/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "FileItem.h"

typedef enum MediaImportChangesetType
{
  MediaImportChangesetTypeNone = 0,
  MediaImportChangesetTypeAdded,
  MediaImportChangesetTypeChanged,
  MediaImportChangesetTypeRemoved
} MediaImportChangesetType;

typedef std::pair<MediaImportChangesetType, CFileItemPtr> ChangesetItemPtr;
typedef std::vector<ChangesetItemPtr> ChangesetItems;
