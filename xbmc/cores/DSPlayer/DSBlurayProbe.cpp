/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DSBlurayProbe.h"

#include "cores/DSPlayer/Utils/DSFileUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

#if defined(HAVE_LIBBLURAY)
#include <libbluray/bluray.h>
#include <libbluray/log_control.h>
#endif

#if defined(HAVE_LIBBLURAY)

namespace
{
// libbluray reports its own errors through a callback. Routing them into our log is
// the only way to see why a disc failed to open or why BD-J could not start.
void BlurayLogCallback(const char* msg)
{
  std::string line(msg ? msg : "");
  StringUtils::Trim(line);
  if (!line.empty())
    CLog::Log(LOGINFO, "libbluray: {}", line);
}

//! \brief Convert a 90 kHz libbluray duration to whole seconds
uint64_t TicksToSeconds(uint64_t ticks)
{
  return ticks / 90000;
}

std::string FormatDuration(uint64_t ticks)
{
  const uint64_t secs = TicksToSeconds(ticks);
  return StringUtils::Format("{:02}:{:02}:{:02}", secs / 3600, (secs / 60) % 60, secs % 60);
}

const char* YesNo(uint8_t value)
{
  return value ? "yes" : "no";
}
} // unnamed namespace

#endif

void CDSBlurayProbe::Probe(const std::string& path)
{
#if !defined(HAVE_LIBBLURAY)
  CLog::Log(LOGWARNING, "{} - built without libbluray, cannot probe \"{}\"", __FUNCTION__,
            path.c_str());
#else
  // Only look at things that could be a disc, everything else is a plain media file
  std::string extension = URIUtils::GetExtension(path);
  StringUtils::ToLower(extension);

  const bool isImage = (extension == ".iso");
  const bool isDiscFolder = (path.find("BDMV") != std::string::npos) || extension.empty();
  if (!isImage && !isDiscFolder)
    return;

  // An unpacked disc is named by the index.bdmv that was selected from the file list, and
  // bd_open() wants the folder the disc's BDMV sits in
  const std::string root = CDSFile::BlurayDiscRoot(path);

  CLog::Log(LOGINFO, "{} - probing \"{}\"", __FUNCTION__, root.c_str());

  // The handler and mask are process wide and stay in effect for playback afterwards, so
  // keep them to errors plus the BD-J startup trace. Anything more (DBG_NAV, DBG_STREAM)
  // logs a line per seek and drowns the log once a disc is playing.
  bd_set_debug_handler(BlurayLogCallback);
  bd_set_debug_mask(DBG_CRIT | DBG_BDJ);

  BLURAY* bd = bd_open(root.c_str(), nullptr);
  if (!bd)
  {
    CLog::Log(LOGWARNING, "{} - libbluray could not open the disc. It is either not a "
                          "Blu-ray or the image could not be read", __FUNCTION__);
    return;
  }

  const BLURAY_DISC_INFO* info = bd_get_disc_info(bd);
  if (!info)
  {
    CLog::Log(LOGWARNING, "{} - no disc info available", __FUNCTION__);
    bd_close(bd);
    return;
  }

  CLog::Log(LOGINFO, "{} - disc name  : {}", __FUNCTION__,
            info->disc_name ? info->disc_name : "(none)");
  CLog::Log(LOGINFO, "{} - volume id  : {}", __FUNCTION__,
            info->udf_volume_id ? info->udf_volume_id : "(none)");

  // Menus. A top menu or first play title is what DSPlayer currently cannot present.
  CLog::Log(LOGINFO, "{} - menus      : top menu {}, first play {}", __FUNCTION__,
            YesNo(info->top_menu != nullptr), YesNo(info->first_play != nullptr));
  CLog::Log(LOGINFO, "{} - titles     : {} total ({} HDMV, {} BD-J, {} unsupported)",
            __FUNCTION__, info->num_titles, info->num_hdmv_titles, info->num_bdj_titles,
            info->num_unsupported_titles);

  // BD-J needs a Java VM. libjvm_detected is the answer to whether JAVA_HOME worked.
  CLog::Log(LOGINFO, "{} - BD-J       : used by disc {}, java vm found {}, usable {}",
            __FUNCTION__, YesNo(info->bdj_detected), YesNo(info->libjvm_detected),
            YesNo(info->bdj_handled));

  // Encryption. Explains discs that open but cannot be read.
  CLog::Log(LOGINFO, "{} - AACS       : encrypted {}, library found {}, handled {} (error {})",
            __FUNCTION__, YesNo(info->aacs_detected), YesNo(info->libaacs_detected),
            YesNo(info->aacs_handled), info->aacs_error_code);
  CLog::Log(LOGINFO, "{} - BD+        : encrypted {}, library found {}, handled {}", __FUNCTION__,
            YesNo(info->bdplus_detected), YesNo(info->libbdplus_detected),
            YesNo(info->bdplus_handled));

  // bd_get_titles() must run first, bd_get_main_title() returns -1 until the title list
  // has been read
  const uint32_t titleCount = bd_get_titles(bd, TITLES_RELEVANT, 0);
  const int mainTitle = bd_get_main_title(bd);
  CLog::Log(LOGINFO, "{} - {} relevant title(s), libbluray considers title {} to be the "
                     "main movie", __FUNCTION__, titleCount, mainTitle);

  for (uint32_t i = 0; i < titleCount; ++i)
  {
    BLURAY_TITLE_INFO* title = bd_get_title_info(bd, i, 0);
    if (!title)
      continue;

    CLog::Log(LOGINFO, "{} -   title {:>3}: playlist {:05}.mpls  {}  {} clip(s), {} angle(s), "
                       "{} chapter(s){}", __FUNCTION__, i, title->playlist,
              FormatDuration(title->duration), title->clip_count, title->angle_count,
              title->chapter_count, (static_cast<int>(i) == mainTitle) ? "  <- main" : "");

    bd_free_title_info(title);
  }

  bd_close(bd);
  CLog::Log(LOGINFO, "{} - probe finished", __FUNCTION__);
#endif
}
