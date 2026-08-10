/*
 *  Copyright (C) 2005-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "BlurayDiscAssetCache.h"

#include "FileItem.h"
#include "FileItemList.h"
#include "ServiceBroker.h"
#include "URL.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/IFileTypes.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

using namespace XFILE;

namespace
{
// Where a mirror lives. Kept next to libbluray's own cache root so that both share whatever
// storage was judged suitable for scratch disc data on this platform.
constexpr const char* MIRROR_ROOT = "special://userdata/cache/bluray/discmirror";
constexpr const char* LIBBLURAY_CACHE_ROOT = "special://userdata/cache/bluray/cache";

// Files read before anything else can happen. Tiny, and re-read on every playlist change.
constexpr std::array TOP_FILES = {"BDMV/index.bdmv", "BDMV/MovieObject.bdmv"};

// Directories to mirror, most valuable first, so that a small size limit still buys the thing it
// was meant to buy. JAR holds the BD-J menu graphics - the multi-megabyte composites whose first
// read is the freeze - and everything after it is small enough to be almost free.
//
// Deliberately absent: STREAM (the video, which is the entire size of the disc) and BACKUP (a
// byte-identical copy of the metadata we are already taking).
constexpr std::array ASSET_DIRS = {"BDMV/JAR",     "BDMV/BDJO", "BDMV/PLAYLIST",
                                   "BDMV/CLIPINF", "BDMV/META", "BDMV/AUXDATA"};

// JAR/ nests a couple of levels on real discs; this is only a runaway guard.
constexpr int MAX_DEPTH = 8;

/*!
 * \brief Lets a copy in progress be abandoned when playback stops.
 *
 * Without it, stopping the disc would block until the current file finished - which over a slow
 * share is exactly the file that is worth several seconds.
 */
class CCopyAbortCallback : public IFileCallback
{
public:
  explicit CCopyAbortCallback(const std::atomic<bool>& stop) : m_stop(stop) {}
  bool OnFileCallback(void*, int, float) override { return !m_stop; }

private:
  const std::atomic<bool>& m_stop;
};
} // namespace

CBlurayDiscAssetCache::CBlurayDiscAssetCache() : CThread("BDAssetCache")
{
}

CBlurayDiscAssetCache::~CBlurayDiscAssetCache()
{
  Stop();
}

std::string CBlurayDiscAssetCache::GetMirrorRoot()
{
  return MIRROR_ROOT;
}

void CBlurayDiscAssetCache::Purge()
{
  for (const auto& dir : {MIRROR_ROOT, LIBBLURAY_CACHE_ROOT})
  {
    if (CDirectory::Exists(dir) && !CDirectory::RemoveRecursive(dir))
      CLog::LogF(LOGWARNING, "failed to clear {}", dir);
  }
}

bool CBlurayDiscAssetCache::Start(const std::string& discRoot)
{
  Stop();

  const auto settings = CServiceBroker::GetSettingsComponent()->GetSettings();
  if (!settings->GetBool(CSettings::SETTING_DISC_CACHEMENUASSETS))
    return false;

  const int limitMB = settings->GetInt(CSettings::SETTING_DISC_CACHEMENUASSETSLIMIT);
  if (limitMB <= 0)
    return false;

  if (discRoot.empty())
    return false;

  m_discRoot = discRoot;
  URIUtils::RemoveSlashAtEnd(m_discRoot);
  m_mirrorRoot = GetMirrorRoot();
  m_budget = static_cast<int64_t>(limitMB) * 1024 * 1024;
  m_copiedBytes = 0;
  m_copiedFiles = 0;

  if (!CDirectory::Create(m_mirrorRoot))
  {
    CLog::LogF(LOGWARNING, "unable to create {} - not caching disc assets", m_mirrorRoot);
    m_discRoot.clear();
    m_mirrorRoot.clear();
    return false;
  }

  Create();
  return true;
}

void CBlurayDiscAssetCache::Stop()
{
  StopThread(true);

  {
    std::unique_lock lock(m_lock);
    m_cached.clear();
  }

  if (!m_mirrorRoot.empty() && CDirectory::Exists(m_mirrorRoot))
    CDirectory::RemoveRecursive(m_mirrorRoot);

  m_discRoot.clear();
  m_mirrorRoot.clear();
}

std::string CBlurayDiscAssetCache::ResolveFile(const std::string& relPath) const
{
  if (m_mirrorRoot.empty())
    return {};

  {
    std::unique_lock lock(m_lock);
    if (m_cached.find(relPath) == m_cached.end())
      return {};
  }

  return URIUtils::AddFileToFolder(m_mirrorRoot, relPath);
}

void CBlurayDiscAssetCache::Process()
{
  // The disc's own reads always win: this is pure prefetch, and stealing bandwidth from the m2ts
  // being played would turn a fixed one-off stall into a stutter.
  SetPriority(ThreadPriority::LOWEST);

  const auto start = std::chrono::steady_clock::now();

  for (const auto* file : TOP_FILES)
  {
    if (m_bStop)
      return;
    CopyFile(file, -1);
  }

  for (const auto* dir : ASSET_DIRS)
  {
    if (m_bStop || !CopyDirectory(dir, 0))
      break;
  }

  if (m_bStop)
    return;

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  CLog::LogF(LOGINFO, "cached {} files ({} KB) from the disc in {} ms", m_copiedFiles,
             m_copiedBytes / 1024, elapsed.count());
}

bool CBlurayDiscAssetCache::CopyDirectory(const std::string& relDir, int depth)
{
  if (depth > MAX_DEPTH)
  {
    CLog::LogF(LOGWARNING, "giving up below {} - too deep", relDir);
    return true;
  }

  CFileItemList items;
  if (!CDirectory::GetDirectory(URIUtils::AddFileToFolder(m_discRoot, relDir), items, "",
                                DIR_FLAG_DEFAULTS))
    return true; // a disc need not carry every directory - BD-J discs alone have JAR and BDJO

  for (const auto& item : items)
  {
    if (m_bStop)
      return false;

    // As in the libbluray directory callback, the label is the entry name; the path is a full URL.
    const std::string relPath = URIUtils::AddFileToFolder(relDir, item->GetLabel());

    if (item->IsFolder())
    {
      if (!CopyDirectory(relPath, depth + 1))
        return false;
    }
    else if (!CopyFile(relPath, item->GetSize()))
    {
      return false; // out of budget, and the priority order means what is left matters least
    }
  }

  return true;
}

bool CBlurayDiscAssetCache::CopyFile(const std::string& relPath, int64_t size)
{
  if (size > m_budget)
  {
    CLog::LogF(LOGINFO,
               "size limit reached after {} files ({} KB) - not caching {} or anything after it",
               m_copiedFiles, m_copiedBytes / 1024, relPath);
    return false;
  }

  const std::string source = URIUtils::AddFileToFolder(m_discRoot, relPath);
  const std::string dest = URIUtils::AddFileToFolder(m_mirrorRoot, relPath);

  if (!CDirectory::Create(URIUtils::GetDirectory(dest)))
  {
    CLog::LogF(LOGWARNING, "unable to create a directory for {}", relPath);
    return true;
  }

  CCopyAbortCallback abort(m_bStop);
  if (!CFile::Copy(source, dest, &abort))
  {
    // Reading through to the disc still works, so a file we could not copy costs nothing but the
    // speed-up. Copy() removes a partial destination itself.
    if (!m_bStop)
      CLog::LogF(LOGWARNING, "unable to cache {}", relPath);
    return !m_bStop;
  }

  // The listed size is what the budget was checked against, but it is not always available (and
  // never is for the files copied before any directory was listed), so charge the budget with what
  // actually landed.
  struct __stat64 stat = {};
  const int64_t copied = CFile::Stat(dest, &stat) == 0 ? stat.st_size : std::max<int64_t>(size, 0);

  m_budget -= copied;
  m_copiedBytes += copied;
  m_copiedFiles++;

  {
    std::unique_lock lock(m_lock);
    m_cached.insert(relPath);
  }

  return true;
}
