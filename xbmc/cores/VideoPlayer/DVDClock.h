/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "threads/CriticalSection.h"

#include <memory>
#include <stdint.h>

class CVideoReferenceClock;

#if HAS_DS_PLAYER
//Time base from directshow is a 100 nanosec unit
#define DS_TIME_BASE 1E7

#define DS_TIME_TO_SEC(x)  ((double)(x / DS_TIME_BASE))
#define DS_TIME_TO_MSEC(x) ((double)(x * 1000 / DS_TIME_BASE))
#define SEC_TO_DS_TIME(x)  ((__int64)(x * DS_TIME_BASE))
#define MSEC_TO_DS_TIME(x) ((__int64)(x * DS_TIME_BASE / 1000))
#define SEC_TO_MSEC(x)     ((double)(x * 1E3))
#endif

class CDVDClock
{
public:

  CDVDClock();
  ~CDVDClock();

  double GetClock(bool interpolated = true);
  double GetClock(double& absolute, bool interpolated = true);

  double ErrorAdjust(double error, const char* log);
  void Discontinuity(double clock, double absolute);
  void Discontinuity(double clock = 0LL)
  {
    Discontinuity(clock, GetAbsoluteClock());
  }

  void Reset() { m_bReset = true; }
  void SetSpeed(int iSpeed);
  void SetSpeedAdjust(double adjust);
  double GetSpeedAdjust() const;

  double GetClockSpeed()
      const; /**< get the current speed of the clock relative normal system time */

  /* tells clock at what framerate video is, to  *
   * allow it to adjust speed for a better match */
  int UpdateFramerate(double fps, double* interval = NULL);

  void SetMaxSpeedAdjust(double speed);

  double GetAbsoluteClock(bool interpolated = true);
  double GetFrequency() { return (double)m_systemFrequency ; }

  bool GetClockInfo(int& MissedVblanks, double& ClockSpeed, double& RefreshRate) const;
  void SetVsyncAdjust(double adjustment);
  double GetVsyncAdjust();

#if HAS_DS_PLAYER
  // Allow a different time base (DirectShow for example use a 100 ns time base)
  void SetTimeBase(int64_t timeBase) { m_timeBase = timeBase; }
  int64_t GetTimeBase() { return m_timeBase; }
#endif

  void Pause(bool pause);
  bool IsPaused() const;
  void Advance(double time);

protected:
  double SystemToAbsolute(int64_t system) const;
  int64_t AbsoluteToSystem(double absolute) const;
  double SystemToPlaying(int64_t system);

  mutable CCriticalSection m_critSection;
  int64_t m_systemUsed;
  int64_t m_startClock;
  int64_t m_pauseClock;
  double m_iDisc;
  bool m_bReset;
  bool m_paused;
  int m_speedAfterPause;
  std::unique_ptr<CVideoReferenceClock> m_videoRefClock;

  int64_t m_systemFrequency;
  int64_t m_systemOffset;
  CCriticalSection m_systemsection;
#if HAS_DS_PLAYER
  int64_t m_timeBase;
#endif

  int64_t m_systemAdjust;
  int64_t m_lastSystemTime;
  double m_speedAdjust;
  double m_vSyncAdjust;
  double m_frameTime;

  double m_maxspeedadjust;
  CCriticalSection m_speedsection;
};
