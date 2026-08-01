/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>

/*!
 * \brief Diagnostic helper that reports what libbluray can see about a disc.
 *
 * DSPlayer hands the disc straight to the LAV Splitter Source, which picks a title on
 * its own. The disc is therefore played as a flat title, its menus are never presented,
 * and there is no way to reach the other titles. CHdmvClipInfo::FindMainMovie exists in
 * the tree but is never called. libbluray provides the navigation that is missing, and
 * is already linked into the build, but nothing in DSPlayer calls it.
 *
 * This probe opens the disc with libbluray and logs what it finds: whether menus and
 * BD-J are present, whether a usable Java VM was located, whether the disc is
 * encrypted, and the list of titles. It builds no graph and changes no playback, it
 * only reports what a libbluray based navigator would have to work with.
 */
class CDSBlurayProbe
{
public:
  /*!
   * \brief Log what libbluray reports about the disc at the given path.
   * \param path Local or UNC path to a disc image or a disc folder. Paths that cannot
   *             be a disc are ignored.
   */
  static void Probe(const std::string& path);
};
