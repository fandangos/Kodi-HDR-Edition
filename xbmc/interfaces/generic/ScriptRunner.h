/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "addons/IAddon.h"
#include "threads/Event.h"

#include <string>

class CScriptRunner
{
protected:
  CScriptRunner() = default;
  virtual ~CScriptRunner() = default;

  virtual bool IsSuccessful() const = 0;
  virtual bool IsCancelled() const = 0;

  ADDON::AddonPtr GetAddon() const { return m_addon; }

  inline bool StartScript(ADDON::AddonPtr addon, const std::string& path)
  {
    return RunScriptInternal(addon, path, 0, false);
  }
  inline bool RunScript(ADDON::AddonPtr addon, const std::string& path, int handle, bool resume)
  {
    return RunScriptInternal(addon, path, handle, resume, true);
  }

  void SetDone();

  static inline int ExecuteScript(ADDON::AddonPtr addon, const std::string& path, bool resume)
  {
    return ExecuteScript(addon, path, -1, resume);
  }
  static int ExecuteScript(ADDON::AddonPtr addon, const std::string& path, int handle, bool resume);

private:
  bool RunScriptInternal(
      ADDON::AddonPtr addon, const std::string& path, int handle, bool resume, bool wait = true);
  bool WaitOnScriptResult(int scriptId, const std::string& path, const std::string& name);

  ADDON::AddonPtr m_addon;

  CEvent m_scriptDone;
};
