/*
 *  Copyright (C) 2005-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DVDDemux.h"
#include "threads/CriticalSection.h"
#include "threads/SystemClock.h"
#include <deque>
#include <map>
#include <memory>
#include <utility>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

class CDVDDemuxFFmpeg;
class CDVDInputStream;
class CURL;
struct ChapterFFmpeg;

enum class TRANSPORT_STREAM_STATE
{
  NONE,
  READY,
  NOTREADY,
};

class CDemuxStreamVideoFFmpeg : public CDemuxStreamVideo
{
public:
  explicit CDemuxStreamVideoFFmpeg(AVStream* stream) : m_stream(stream) {}
  std::string GetStreamName() override;

  std::string m_description;
protected:
  AVStream* m_stream = nullptr;
};

class CDemuxStreamAudioFFmpeg : public CDemuxStreamAudio
{
public:
  explicit CDemuxStreamAudioFFmpeg(AVStream* stream) : m_stream(stream) {}
  std::string GetStreamName() override;

  std::string m_description;
protected:
  CDVDDemuxFFmpeg* m_parent;
  AVStream* m_stream  = nullptr;
};

class CDemuxStreamSubtitleFFmpeg
  : public CDemuxStreamSubtitle
{
public:
  explicit CDemuxStreamSubtitleFFmpeg(AVStream* stream) : m_stream(stream) {}
  std::string GetStreamName() override;

  std::string m_description;
protected:
  CDVDDemuxFFmpeg* m_parent;
  AVStream* m_stream = nullptr;
};

class CDemuxParserFFmpeg
{
public:
  ~CDemuxParserFFmpeg();
  AVCodecParserContext* m_parserCtx = nullptr;
  AVCodecContext* m_codecCtx = nullptr;
};

#define FFMPEG_DVDNAV_BUFFER_SIZE 2048  // for dvd's

struct StereoModeConversionMap;

class CDVDDemuxFFmpeg : public CDVDDemux
{
public:
  CDVDDemuxFFmpeg();
  ~CDVDDemuxFFmpeg() override;

  bool Open(const std::shared_ptr<CDVDInputStream>& pInput, bool fileinfo);
  void Dispose();
  bool Reset() override ;
  void Flush() override;
  void Abort() override;
  void SetSpeed(int iSpeed) override;
  std::string GetFileName() override;

  DemuxPacket* Read() override;
  DemuxPacket* ReadInternal(bool keep);

  bool SeekTime(double time, bool backwards = false, double* startpts = NULL) override;
  bool SeekByte(int64_t pos);
  int GetStreamLength() override;
  CDemuxStream* GetStream(int iStreamId) const override;
  std::vector<CDemuxStream*> GetStreams() const override;
  int GetNrOfStreams() const override;
  int GetPrograms(std::vector<ProgramInfo>& programs) override;
  void SetProgram(int progId) override;

  bool SeekChapter(int chapter, double* startpts = NULL) override;
  int GetChapterCount() override;
  int GetChapter() override;
  void GetChapterName(std::string& strChapterName, int chapterIdx=-1) override;
  std::chrono::milliseconds GetChapterPos(int chapterIdx = -1) override;
  std::string GetStreamCodecName(int iStreamId) override;

  bool Aborted();

  AVFormatContext* m_pFormatContext;
  std::shared_ptr<CDVDInputStream> m_pInput;

protected:
  friend class CDemuxStreamAudioFFmpeg;
  friend class CDemuxStreamVideoFFmpeg;
  friend class CDemuxStreamSubtitleFFmpeg;

  CDemuxStream* AddStream(int streamIdx);
  void AddStream(int streamIdx, CDemuxStream* stream);
  void CreateStreams(unsigned int program = UINT_MAX);
  void DisposeStreams();
  void ParsePacket(AVPacket* pkt);
  TRANSPORT_STREAM_STATE TransportStreamAudioState();
  TRANSPORT_STREAM_STATE TransportStreamVideoState();
  bool IsTransportStreamReady();
  void ResetVideoStreams();
  AVDictionary* GetFFMpegOptionsFromInput();
  double ConvertTimestamp(int64_t pts, int den, int num);
  bool IsProgramChange();
  unsigned int HLSSelectProgram();

  std::string GetStereoModeFromMetadata(AVDictionary* pMetadata);
  std::string ConvertCodecToInternalStereoMode(const std::string& mode, const StereoModeConversionMap* conversionMap);

  void GetL16Parameters(int& channels, int& samplerate);
  double SelectAspect(AVStream* st, bool& forced);

  StreamHdrType DetermineHdrType(AVStream* pStream);

#ifdef HAVE_LIBDOVI
  // Dolby Vision profile 7 UHD Blu-ray: merge the enhancement-layer RPU into the base
  // layer and present it as single-layer profile 8.1 so Android MediaCodec outputs real
  // Dolby Vision instead of the HDR10 base layer. See ReadInternal() for the packet flow.
  void SetupDoviProfile7Merge(int elStreamIndex);
  bool ExtractConvertedDoviRpu(const uint8_t* elData, int elSize, std::vector<uint8_t>& rpuNal);
  // Splices rpuNal onto the end of bl's access unit. Consumes bl, returns the merged packet.
  DemuxPacket* MergeDoviRpu(DemuxPacket* bl, const std::vector<uint8_t>& rpuNal);
  // Marks the queued base layer that this timestamp belongs to as resolved. Returns false
  // if that frame has not been demuxed yet.
  bool ResolveDoviPending(double key, std::vector<uint8_t>& rpuNal);
  // Emits the oldest base layer once its enhancement layer has been seen (or once the
  // queue is too deep to keep waiting). Returns nullptr while the front is still waiting.
  DemuxPacket* DrainDoviPending();
  // Frees every queued base layer and RPU.
  void ClearDoviPending();
#endif

  CCriticalSection m_critSection;
  std::map<int, CDemuxStream*> m_streams;
  std::map<int, std::unique_ptr<CDemuxParserFFmpeg>> m_parsers;

  AVIOContext* m_ioContext;

  double   m_currentPts; // used for stream length estimation
  bool     m_bMatroska;
  bool     m_bAVI;
  bool     m_bSup;
  int      m_speed;
  unsigned int m_program;
  unsigned int m_streamsInProgram;
  unsigned int m_newProgram;
  unsigned int m_initialProgramNumber;
  int m_seekStream;

  XbmcThreads::EndTime<> m_timeout;

  // Due to limitations of ffmpeg, we only can detect a program change
  // with a packet. This struct saves the packet for the next read and
  // signals STREAMCHANGE to player
  struct
  {
    AVPacket pkt;       // packet ffmpeg returned
    int      result;    // result from av_read_packet
  }m_pkt;

#ifdef HAVE_LIBDOVI
  // Dolby Vision profile 7 -> 8.1 enhancement-layer RPU merge state
  bool m_dvP7Merge = false;
  int m_dvP7BlIndex = -1; // ffmpeg stream index of the Dolby Vision base layer
  int m_dvP7ElIndex = -1; // ffmpeg stream index of the Dolby Vision enhancement layer

  // A base-layer frame waiting for the enhancement layer that carries its RPU.
  struct DvP7Pending
  {
    DemuxPacket* bl = nullptr;
    std::vector<uint8_t> rpu; // empty once resolved = its EL carried no usable RPU
    bool resolved = false; // its enhancement layer has been seen
  };
  // Base layers in demux order. Only the FRONT is ever emitted, so the merge can
  // never reorder the stream. Depth is what lets it survive a disc that delivers the
  // layers in bursts (BL BL EL EL) instead of strictly interleaved.
  std::deque<DvP7Pending> m_dvP7Pending;
  // RPUs whose base layer has not been demuxed yet, keyed by timestamp.
  std::deque<std::pair<double, std::vector<uint8_t>>> m_dvP7EarlyRpu;
  // Deep enough for any real interleave; past this the oldest frame is emitted
  // unpaired rather than let the video stream starve.
  static constexpr size_t DVP7_MAX_PENDING = 8;

  // diagnostics for the merge
  uint64_t m_dvP7MergedCount = 0;  // BL frames emitted with their own converted RPU
  uint64_t m_dvP7NoRpuCount = 0;   // BL frames whose paired EL carried no usable RPU
  uint64_t m_dvP7BlNoElCount = 0;  // BL frames emitted unpaired - the defect metric
  uint64_t m_dvP7ElNoBlCount = 0;  // RPUs discarded without ever finding their frame
  uint64_t m_dvP7LastLogCount = 0;
#endif

  bool m_streaminfo;
  bool m_reopen = false;
  bool m_checkTransportStream;
  int m_displayTime = 0;
  double m_dtsAtDisplayTime;
  bool m_seekToKeyFrame = false;
  double m_startTime = 0;
  std::vector<ChapterFFmpeg> m_chapters;
};
