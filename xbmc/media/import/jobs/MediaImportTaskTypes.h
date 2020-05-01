/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

enum class MediaImportTaskType
{
  LocalItemsRetrieval,
  ImportItemsRetrieval,
  Changeset,
  Synchronisation,
  Cleanup,
  Update,
  Removal
};

class MediaImportTaskTypes
{
public:
  MediaImportTaskTypes() = delete;

  static std::string ToString(MediaImportTaskType taskType)
  {
    switch (taskType)
    {
      case MediaImportTaskType::LocalItemsRetrieval:
        return "local items retrieval";

      case MediaImportTaskType::ImportItemsRetrieval:
        return "import items retrieval";

      case MediaImportTaskType::Changeset:
        return "changeset";

      case MediaImportTaskType::Synchronisation:
        return "synchronisation";

      case MediaImportTaskType::Cleanup:
        return "cleanup";

      case MediaImportTaskType::Update:
        return "update";

      case MediaImportTaskType::Removal:
        return "removal";

      default:
        break;
    }

    return "unknown";
  }
};
