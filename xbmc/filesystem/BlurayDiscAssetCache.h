/*
 *  Copyright (C) 2005-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "threads/CriticalSection.h"
#include "threads/Thread.h"

#include <set>
#include <string>

/*!
 * \brief Background mirror of a disc's non-video files to local storage.
 *
 * Menu assets are read from the disc lazily, on the thread that is drawing the menu: opening a
 * BD-J page for the first time can pull several megabytes of PNG composites synchronously, which
 * over a network share is long enough to see as a freeze. Every later open is fast because
 * libbluray's own VFSCache kept a copy - the cost is only ever paid once, on first access.
 *
 * This copies those files to local storage up front instead, in the background, while playback
 * starts normally. It is not a second cache: it sits underneath libbluray's file-access callbacks,
 * so it speeds up both the native reads (index/MovieObject/mpls/clpi/bdjo, which VFSCache never
 * sees) and VFSCache's own lazy fill, which turns from a network fetch into a local copy.
 *
 * Video is never mirrored, and neither is BACKUP - it is a byte-identical mirror of what we copy
 * anyway, so it would double the payload for nothing.
 */
class CBlurayDiscAssetCache : private CThread
{
public:
  CBlurayDiscAssetCache();
  ~CBlurayDiscAssetCache() override;

  /*!
   * \brief Discard every mirror, plus libbluray's own cache root.
   *
   * Called before a disc is opened so each playback starts from a genuinely cold cache: a mirror
   * left behind by a crash would otherwise serve stale files, and a warm VFSCache would hide the
   * very first-access cost this class exists to remove (which also makes it impossible to measure
   * whether it is working). Neither directory holds anything that must survive - the disc's own
   * persistent storage is a separate root and is left alone.
   */
  static void Purge();

  /*!
   * \brief Begin mirroring \a discRoot in the background.
   * \param discRoot Disc root as a Kodi VFS path, i.e. the directory holding BDMV.
   * \return True if the mirror was started.
   */
  bool Start(const std::string& discRoot);

  /*! \brief Stop mirroring and delete everything copied so far. */
  void Stop();

  /*!
   * \brief Map a disc-relative path to its local copy.
   * \param relPath Path relative to the disc root, e.g. "BDMV/JAR/00001/Menu.png".
   * \return The local path, or an empty string if the file has not been mirrored.
   *
   * Called from libbluray's threads, so it is safe to call while the mirror is still filling.
   * Only files that were copied in full are ever resolved.
   */
  std::string ResolveFile(const std::string& relPath) const;

private:
  void Process() override;

  /*! \brief Mirror \a relDir and everything under it. \return False to abandon the mirror. */
  bool CopyDirectory(const std::string& relDir, int depth);
  bool CopyFile(const std::string& relPath, int64_t size);

  static std::string GetMirrorRoot();

  std::string m_discRoot;
  std::string m_mirrorRoot;

  // Prefetch thread only.
  int64_t m_budget{0};
  int64_t m_copiedBytes{0};
  unsigned int m_copiedFiles{0};

  mutable CCriticalSection m_lock;
  std::set<std::string> m_cached; // disc-relative paths that are present in the mirror, in full
};
