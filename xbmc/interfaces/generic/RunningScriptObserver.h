/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "threads/Event.h"
#include "threads/Thread.h"

class CRunningScriptObserver : public CThread
{
public:
  CRunningScriptObserver(int scriptId, CEvent& evt);
  ~CRunningScriptObserver() = default;

  void Abort();

protected:
  // implementation of CThread
  void Process() override;

  int m_scriptId;
  CEvent& m_event;
};
