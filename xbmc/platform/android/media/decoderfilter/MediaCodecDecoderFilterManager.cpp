/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */


/**
 * \file media\hwdecoder\DecoderFilterManager.cpp
 * \brief Implements CDecoderFilterManager class.
 *
 */

#include "MediaCodecDecoderFilterManager.h"

#include "utils/StringUtils.h"
#include "utils/log.h"

#include <androidjni/MediaCodecList.h>


CMediaCodecDecoderFilterManager::CMediaCodecDecoderFilterManager()
{
  static const char *blacklisted_decoders[] = {
    // No software decoders
    "OMX.google",
    // For Rockchip non-standard components
    "AVCDecoder",
    "AVCDecoder_FLASH",
    "FLVDecoder",
    "M2VDecoder",
    "M4vH263Decoder",
    "RVDecoder",
    "VC1Decoder",
    "VPXDecoder",
    // End of Rockchip
    NULL
  };

  const std::vector<CJNIMediaCodecInfo> codecInfos =
      CJNIMediaCodecList(CJNIMediaCodecList::REGULAR_CODECS).getCodecInfos();

  for (const CJNIMediaCodecInfo& codec_info : codecInfos)
  {
    if (codec_info.isEncoder())
      continue;

    std::string codecname = codec_info.getName();
    // FLAG_STILLS_ALLOWED is granted here as well. VideoPlayer sets CDVDStreamInfo::stills for
    // any stream opened while a disc menu is on screen, and without this flag isValid() rejects
    // every hardware decoder for that stream, so a Blu-ray's 4K HEVC menu background falls back
    // to software decoding. A userdata decoderfilter.xml still overrides this: the base class
    // constructor Load()s it first and add() will not replace an entry that already exists.
    uint32_t flags = CDecoderFilter::FLAG_GENERAL_ALLOWED | CDecoderFilter::FLAG_DVD_ALLOWED |
                     CDecoderFilter::FLAG_STILLS_ALLOWED;
    for (const char **ptr = blacklisted_decoders; *ptr && flags; ptr++)
    {
      if (!StringUtils::CompareNoCase(*ptr, codecname, strlen(*ptr)))
        flags = 0;
    }
    std::string tmp(codecname);
    StringUtils::ToLower(tmp);
    int minheight = 0;
    if (tmp.find("mpeg4") != std::string::npos)
      minheight = 720;
    else if (tmp.find("mpeg2") != std::string::npos)
      minheight = 720;
    else if (tmp.find("263") != std::string::npos)
      minheight = 720;

    add(CDecoderFilter(codecname, flags, minheight));
    CLog::Log(LOGINFO, "Mediacodec decoder: {}", codecname);
  }
  Save();
}

