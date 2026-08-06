/*
 *  Copyright (C) 2016-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PlatformAndroid.h"

#include "ServiceBroker.h"
#include "filesystem/SpecialProtocol.h"
#include "settings/AdvancedSettings.h"
#include "settings/SettingsComponent.h"
#include "utils/log.h"
#include "windowing/android/WinSystemAndroidGLESContext.h"

#include "platform/android/activity/XBMCApp.h"
#include "platform/android/powermanagement/AndroidPowerSyscall.h"
#include "platform/android/storage/AndroidStorageProvider.h"

#include <stdlib.h>

#include <androidjni/Build.h>
#include <androidjni/Environment.h>
#include <androidjni/PackageManager.h>

CPlatform* CPlatform::CreateInstance()
{
  return new CPlatformAndroid();
}

bool CPlatformAndroid::InitStageOne()
{
  if (!CPlatformPosix::InitStageOne())
    return false;
  setenv("SSL_CERT_FILE", CSpecialProtocol::TranslatePath("special://xbmc/system/certs/cacert.pem").c_str(), 1);

  setenv("OS", "Linux", true); // for python scripts that check the OS

  // Blu-ray BD-J: point libbluray at the OpenJDK JRE and BD-J jars bundled in the APK.
  // The JRE ships in the APK assets as assets/j2re-image and Splash extracts the whole
  // asset tree to special://xbmc (internal, persistent app storage, re-staged on every
  // app update) before we run here. libbluray reads these with getenv() when a BD-J disc
  // is opened, so they must be set in C: Kodi's Android CPython is built without
  // putenv/setenv, so os.environ from a Python addon never reaches the process env.
  const std::string jreHome = CSpecialProtocol::TranslatePath("special://xbmc/j2re-image/");
  setenv("JAVA_HOME", jreHome.c_str(), 1);
  setenv("JDK_HOME", jreHome.c_str(), 1);
  setenv("LIBBLURAY_CP", jreHome.c_str(), 1); // BD-J jars (libbluray-*-j2se-<ver>.jar) live here
  // -Xint (interpreter only) is REQUIRED on Android: HotSpot's JIT relies on catching
  // SIGSEGV for implicit null checks / safepoint polling, but Android's ART installs its
  // own signal chain (libsigchain) and does not hand those SIGSEGVs back to the JVM - it
  // aborts the whole process ("exiting due to SIG_DFL handler for signal 11") when a BD-J
  // Xlet exercises JIT-compiled code (e.g. a disc's chapter-select menu). The interpreter
  // uses explicit null checks (proper NullPointerExceptions), avoiding the SEGV traps.
  setenv("_JAVA_OPTIONS", ("-Xint -Djava.io.tmpdir=" + jreHome).c_str(), 1);

  CWinSystemAndroidGLESContext::Register();

  CAndroidPowerSyscall::Register();

  return true;
}

bool CPlatformAndroid::InitStageThree()
{
  if (!CPlatformPosix::InitStageThree())
    return false;

  if (CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_guiVideoLayoutTransparent)
  {
    CLog::Log(LOGINFO, "XBMCApp: VideoLayout view was set to transparent.");
    CXBMCApp::Get().SetVideoLayoutBackgroundColor(0);
  }

  return true;
}

void CPlatformAndroid::PlatformSyslog()
{
  CLog::Log(
      LOGINFO,
      "Product: {}, Device: {}, Board: {} - Manufacturer: {}, Brand: {}, Model: {}, Hardware: {}",
      CJNIBuild::PRODUCT, CJNIBuild::DEVICE, CJNIBuild::BOARD, CJNIBuild::MANUFACTURER,
      CJNIBuild::BRAND, CJNIBuild::MODEL, CJNIBuild::HARDWARE);

  std::string extstorage;
  const bool extready = CAndroidStorageProvider::GetExternalStorage(extstorage);
  CLog::Log(
      LOGINFO, "External storage path = {}; status = {}; Permissions = {}{}", extstorage,
      extready ? "ok" : "nok",
      CJNIEnvironment::isExternalStorageManager() ? "MANAGE_EXTERNAL_STORAGE " : "",
      CJNIContext::checkCallingOrSelfPermission("android.permission.WRITE_EXTERNAL_STORAGE") ==
              CJNIPackageManager::PERMISSION_GRANTED
          ? "WRITE_EXTERNAL_STORAGE"
          : "");
}
