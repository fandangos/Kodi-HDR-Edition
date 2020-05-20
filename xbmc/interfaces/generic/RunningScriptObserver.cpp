/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RunningScriptObserver.h"

#include "interfaces/generic/ScriptInvocationManager.h"

CRunningScriptObserver::CRunningScriptObserver(int scriptId, CEvent& evt)
  : CThread("ScriptObs"), m_scriptId(scriptId), m_event(evt)
{
  Create();
}

void CRunningScriptObserver::Process()
{
  while (!m_bStop)
  {
    if (!CScriptInvocationManager::GetInstance().IsRunning(m_scriptId))
    {
      m_event.Set();
      break;
    }

    CThread::Sleep(20);
  }
}

void CRunningScriptObserver::Abort()
{
  m_bStop = true;
}
