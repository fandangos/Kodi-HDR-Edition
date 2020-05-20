/*
 *  Copyright (C) 2013-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

using MediaType = std::string;
using MediaTypes = std::set<MediaType>;
using GroupedMediaTypes = std::vector<MediaType>;

#define MediaTypeNone             ""
#define MediaTypeMusic            "music"
#define MediaTypeArtist           "artist"
#define MediaTypeAlbum            "album"
#define MediaTypeSong             "song"
#define MediaTypeVideo            "video"
#define MediaTypeVideoCollection  "set"
#define MediaTypeMusicVideo       "musicvideo"
#define MediaTypeMovie            "movie"
#define MediaTypeTvShow           "tvshow"
#define MediaTypeSeason           "season"
#define MediaTypeEpisode          "episode"

class CMediaTypes
{
public:
  static bool IsValidMediaType(const MediaType &mediaType);
  static bool IsMediaType(const std::string &strMediaType, const MediaType &mediaType);
  static MediaType FromString(const std::string &strMediaType);
  static MediaType ToPlural(const MediaType &mediaType);

  static bool IsContainer(const MediaType &mediaType);

  static std::string GetLocalization(const MediaType &mediaType);
  static std::string GetPluralLocalization(const MediaType &mediaType);
  static std::string GetCapitalLocalization(const MediaType &mediaType);
  static std::string GetCapitalPluralLocalization(const MediaType &mediaType);

  static std::string Join(const MediaTypes& mediaTypes);
  static std::string Join(const GroupedMediaTypes& mediaTypes);
  static GroupedMediaTypes Split(const std::string& mediaTypes);

  static std::string ToLabel(const GroupedMediaTypes& mediaTypes);

private:
  typedef struct MediaTypeInfo {
    MediaTypeInfo(const MediaType &mediaType, const std::string &plural, bool container,
      int localizationSingular, int localizationPlural,
      int localizationSingularCapital, int localizationPluralCapital)
      : mediaType(mediaType)
      , plural(plural)
      , container(container)
      , localizationSingular(localizationSingular)
      , localizationPlural(localizationPlural)
      , localizationSingularCapital(localizationSingularCapital)
      , localizationPluralCapital(localizationPluralCapital)
    { }

    MediaType mediaType;
    std::string plural;
    bool container;
    int localizationSingular;
    int localizationPlural;
    int localizationSingularCapital;
    int localizationPluralCapital;
  } MediaTypeInfo;

  static std::map<std::string, MediaTypeInfo>::const_iterator findMediaType(const std::string &mediaType);

  static std::map<std::string, MediaTypeInfo> m_mediaTypes;
};
