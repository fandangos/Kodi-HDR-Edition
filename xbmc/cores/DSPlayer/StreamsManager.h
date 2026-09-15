#pragma once

/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
 *
 *		Copyright (C) 2010-2013 Eduard Kytmanov
 *		http://www.avmedia.su
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef HAS_DS_PLAYER
#error DSPlayer's header file included without HAS_DS_PLAYER defined
#endif

#include "DSUtil/DSUtil.h"
#include "DSUtil/DShowCommon.h"
#include "DSUtil/MediaTypeEx.h"
#include <initguid.h>
#include "moreuuids.h"
#include <dmodshow.h>
#include <D3d9.h>

#include "DSGraph.h"
#include "utils/log.h"
#include "utils/CharsetConverter.h"
#include "utils/RegExp.h"
#include "Subtitles/subpic/ISubPic.h"
#include "Subtitles/DllLibSubs.h"
#include "Subtitles/ILogImpl.h"

#include "utils/StreamDetails.h"
#include "utils/Observer.h"
#include "Filters/IDirectVobSub.h"
#include "Filters/LAVAudioSettings.h"


#include <cassert>
#include "threads/CriticalSection.h"

#define XYVSFILTER_SUB_EXTERNAL 6590018
#define XYVSFILTER_SUB_INTERNAL 6590016

interface __declspec(uuid("5711A92E-913E-45A5-9714-8481887644E3")) IBdStreamSelect : public IUnknown
{
  STDMETHOD_(UINT, GetTitlesCount)() PURE;
  STDMETHOD_(UINT, GetSelTitle)() PURE;
  STDMETHOD(SetSelTitle)(UINT idx) PURE;
  STDMETHOD(GetTitleInfo)(UINT idx, LPUINT pidx, REFERENCE_TIME *prtDuration) PURE;
};

class CDSStreamDetail
{
public:
  CDSStreamDetail();

  uint32_t                IAMStreamSelect_Index; ///< IAMStreamSelect index of the stream
  std::string              displayname; ///< Stream displayname
  unsigned long           flags; ///< Stream flags. Set to AMSTREAMSELECTINFO_ENABLED if the stream if selected in the GUI, 0 otherwise
  Com::SComPtr<IPin>     pObj; ///< Output pin of the splitter
  Com::SComPtr<IPin>     pUnk; ///< Not used
  LCID                    lcid; ///< LCID of stream language
  DWORD                   group; ///< Currently not used
  bool                    connected; ///< Is the stream connected
};

//  std::string codecname; ///< Stream codec name
//  std::string codec; ///< Stream codec ID

/// Informations about an audio stream
class CDSStreamDetailAudio : public CDSStreamDetail, public CStreamDetailAudio
{
public:
  CDSStreamDetailAudio();

  std::string              m_strCodecName;
  uint32_t                m_iBitRate; ///< Audio bitrate
  uint32_t                m_iSampleRate; ///< Audio samplerate
};

/// Informations about a video stream
struct CDSStreamDetailVideo : public CDSStreamDetail, public CStreamDetailVideo
{
public:
  std::string              m_strCodecName;

  CDSStreamDetailVideo();
};
/// Informations about a internal subtitle
enum SubtitleType {
  INTERNAL,
  EXTERNAL
};

enum SelectSubType {
  ADD_EXTERNAL_SUB,
  SELECT_INTERNAL_SUB
};

class CDSStreamDetailSubtitle : public CDSStreamDetail, public CStreamDetailSubtitle
{
public:
  CDSStreamDetailSubtitle(SubtitleType type = INTERNAL);

  std::string           encoding; ///< Subtitle encoding
  uint32_t              offset; ///< Not used
  std::string           isolang; ///< ISO Code of the subtitle language.
  std::string           trackname; ///< 
  GUID                  subtype;
  const SubtitleType    m_subType;
};

class CDSStreamDetailSubfilter : public CDSStreamDetail, public CStreamDetailSubtitle
{
public:
  CDSStreamDetailSubfilter(SubtitleType type);

  std::string           encoding; ///< Subtitle encoding
  uint32_t              offset; ///< Not used
  std::string           isolang; ///< ISO Code of the subtitle language.
  std::string           trackname; ///< 
  GUID                  subtype;
  const SubtitleType    m_subType;
};


struct CDSStreamDetailEdition : public CDSStreamDetail, public CStreamDetailEditon
{
public:

  REFERENCE_TIME      m_rtDuration;
  CDSStreamDetailEdition()
    : m_rtDuration(_I64_MIN)
  {

  };
  bool IsWorseThan(const CStreamDetail& that) const
  {
    //TODO
    return false;
  };
  void Archive(CArchive& ar)
  {
    //TODO
  }

};

/// Informations about an external subtitle
class CDSStreamDetailSubtitleExternal : public CDSStreamDetailSubtitle
{
public:

  CDSStreamDetailSubtitleExternal();

  std::string            path; ///< Subtitle file path
  Com::SComPtr<ISubStream> substream;
};

/** @brief DSPlayer Streams Manager.
 *
 * Singleton class handling audio and subtitle streams.
 */
/*!
 * \brief Holds the viewer's whole subtitle appearance where the filter reads it, for as long
 *        as it takes the filter to be created
 *
 * The subtitle filter reads a style out of the registry once, in its constructor, and that one
 * string carries everything: the colour and opacity of the text, of the border, of the
 * background box and of the shadow, the width of each, the blur. The interfaces it offers
 * afterwards carry a fraction of that -- a font, one colour, and a border and a shadow as no
 * more than on or off -- which is why setting the appearance through
 * CStreamsManager::ApplySubtitleStyle alone leaves the viewer's opacity with nowhere to go.
 *
 * So the appearance is written where the filter will look, the filter is created, and what was
 * there before is put straight back. That string is shared with every other player using the
 * same filter, and it is theirs: Kodi borrows it for the length of one call rather than
 * standing on it. Whatever the viewer set in the filter's own property page and Kodi has no
 * opinion about -- the margins, the alignment, the angles -- is read out of it and written
 * back, so only the appearance Kodi does have settings for is changed.
 */
class CSubtitleFilterStyle
{
public:
  //! \param filterName The filter about to be created, since only two of them read a style
  explicit CSubtitleFilterStyle(const std::string& filterName);
  ~CSubtitleFilterStyle();

  CSubtitleFilterStyle(const CSubtitleFilterStyle&) = delete;
  CSubtitleFilterStyle& operator=(const CSubtitleFilterStyle&) = delete;

  /*!
   * \brief Whether the last filter to be created was given the whole appearance this way
   *
   * When it was, there is nothing left for the narrower interface to add, and asking it would
   * only coarsen the border and the shadow back to a fixed width.
   */
  static bool WasPublished() { return m_published; }

private:
  std::wstring m_key;      //!< Where the filter reads its style, empty when it reads none
  std::wstring m_previous; //!< What was there before, to be put back
  bool m_hadPrevious = false;
  bool m_written = false;

  static bool m_published;
};

class CStreamsManager : public Observer
{
public:
  friend class CSubtitleManager;
  /// Retrieve singleton instance
  static CStreamsManager *Get();
  /// Create singleton instance
  static bool Create();
  /// Destroy singleton instance
  static void Destroy();

  /// @return A std::vector of all audio streams found in the media file
  std::vector<CDSStreamDetailAudio *>& GetAudios();

  /// @return Audio streams count
  int  GetAudioStreamCount();
  /// @return The index to the current audio stream
  int  GetAudioStream();
  /** Get the displayname of an audio stream
   * @param[in] iStream Index of the audio stream to get displayname of
   * @param[out] strStreamName Name of the iStream audio stream
   */
  void GetAudioStreamName(int iStream, std::string &strStreamName);
  /** Get the ISO 639 language code of an audio stream
   * @param[in] iStream Index of the audio stream to get the language of
   * @param[out] strStreamLang ISO 639 code of the iStream audio stream, empty if unknown
   */
  void GetAudioStreamLanguage(int iStream, std::string &strStreamLang);
  /** Change the current audio stream
   * @param[in] iStream Index of the audio stream
   * @remarks If the IAMStreamSelect interface wasn't found, the graph must be stopped and restarted in order to change the audio stream
   */
  void SetAudioStream(int iStream);
  /// @return Audio streams count

  void GetVideoStreamName(std::string &strStreamName);

  int  GetSubtitleCount();
  int  GetSubtitle();
  void GetSubtitleInfo(int iStream, SubtitleStreamInfo& strStreamName);
  bool GetSubtitleVisible();
  void SetSubtitleVisible(bool bVisible);

  /*!
   * \brief Stop or resume the subtitle filter drawing, without touching the viewer's setting
   *
   * A DVD needs the two separated. Its own subtitles are drawn by CDSDvdNavigator and the
   * filter is held quiet throughout so that the graphs where LAV does find a subpicture pin
   * -- menus, mostly -- do not draw the same picture again underneath. Choosing a subtitle
   * *file* on such a disc has to lift that, and choosing one of the disc's own has to put it
   * back, and neither of those is the viewer turning subtitles off.
   */
  void ShowSubtitleFilter(bool bVisible);

  //! \brief How many of the tracks offered are subtitle files rather than the splitter's
  int GetExternalSubtitleCount();
  //! \brief Where the nth subtitle file sits in the list of tracks, -1 if there is no such
  int ExternalSubtitle(int nth);
  //! \brief Which subtitle file a track is, counting files only, -1 when it is not one
  int SubtitleIsExternal(int iStream);

  /*!
   * \brief Hand the subtitle appearance the viewer set to the filter that draws subtitles
   *
   * Nothing else in Kodi reaches these subtitles. The font, size and colour under Settings
   * are read by Kodi's own renderer, and with a subtitle filter in the graph that renderer
   * never runs -- XySubFilter draws through madVR instead, out of a style of its own that
   * until now was whatever the filter had in its registry. Every one of those settings did
   * nothing, which is exactly how it looked.
   *
   * Only part of the appearance can be said: see the body for what the interface carries and
   * what it has no room for.
   */
  void ApplySubtitleStyle();

  //! \brief The viewer changed a subtitle setting while something was playing
  void Notify(const Observable& obs, const ObservableMessage msg) override;

  bool SetSubtitle(const std::string &sTrackName);
  void SetSubtitle(int iStream);
  void SelectBestSubtitle(const std::string &fileName = "");
  void SelectBestAudio();
  int AddSubtitle(const std::string& subFilePath);
  void SetAVDelay(float fValue = 0.0f, int iDisplayerLatency = 0);
  float GetAVDelay();
  int GetLastAVDelay() { return m_lastDelay; };
  void SetAudioInterface();
  void resetDelayInterface();
  void SetSubTitleDelay(float fValue = 0.0f);
  float GetSubTitleDelay();

  int  GetEditionsCount();
  int  GetEdition();
  void GetEditionInfo(int iEdition, std::string &strEditionName, REFERENCE_TIME *prt);
  void SetEdition(int iEdition);
  bool IsMatroskaEditions(){ return m_mkveditions; };

  std::string GetStereoMode();

  DWORD GetHWAccel();

  /** Wait until the graph is ready. If a stream is being changed, the
   * function waits. Otherwise, simply returns.
   */
  void WaitUntilReady();
  /// @return The number of audio channels of the current audio stream
  int GetChannels();
  int GetChannels(int istream);
  /// @return The number of bits per sample of the current audio stream
  int GetBitsPerSample(int istream);
  /// @return The sample rate of the current audio stream
  int GetSampleRate(int istream);
  /// @return The ID of the audio codec used in the media file (ie FLAC, MP3, DTS ...)
  std::string GetAudioCodecName(int istream);
  /// @return The displayname of the audio codec used in the media file (ie FLAC, MP3, DTS ...)
  std::string GetAudioCodecDisplayName();
  std::string GetAudioCodecDisplayName(int istream);
  /// @return An instance to the IAMStreamSelect interface if the splitter expose it, NULL otherwise
  IAMStreamSelect *GetStreamSelector() { return m_pIAMStreamSelect; }

  /*!
   * \brief Initialize streams from the media file a graph has just been built for
   * \param file What is being opened
   *
   * The file is passed in rather than asked of the application, which does not know yet:
   * g_application.CurrentFile() is still empty while the graph is being built, so the scan
   * for subtitle files beside it found nothing and said nothing about it either -- CUtil's
   * scan gives up on a path that is not a video before it logs a word.
   */
  void LoadStreams(const std::string& file = "");

  /// @return Current video width
  int GetPictureWidth();
  /// @return Current video height
  int GetPictureHeight();
  /// @return The ID of the video codec used in the media file (XviD, DivX, h264, ...)
  std::string GetVideoCodecName();
  /// @return The displayname of the video codec used in the media file (XviD, DivX, h264, ...)
  std::string GetVideoCodecDisplayName() { return m_videoStream.m_strCodecName; }

  /** Initialize the manager
   * @return True if the manager is initialized, false otherwise
   */
  virtual bool InitManager();
  /** Extract stream information from AM_MEDIA_TYPE
   * @param[in] mt Media type informations
   * @param[out] s A filled SStreamInfos structure
   */
  static void MediaTypeToStreamDetail(AM_MEDIA_TYPE *mt, CStreamDetail& s);
  static void FormatStreamName(CStreamDetail& s);
  static void FormatStreamNameBySplitter(CStreamDetail& s);
  static void ExtractCodecDetail(CStreamDetail& s, std::string& codecInfos);
  static std::string ISOToLanguage(std::string code);

  void LoadDVDStreams();
  void UpdateDVDStream();

  CDSStreamDetailVideo* GetVideoStreamDetail(unsigned int iIndex = 0);
  CDSStreamDetailAudio* GetAudioStreamDetail(unsigned int iIndex = 0);

  std::shared_ptr<CSubtitleManager> SubtitleManager;

protected:
  CStreamsManager(void);
  ~CStreamsManager(void);
  static CStreamsManager *m_pSingleton;

  void LoadStreamsInternal();
  void LoadIAMStreamSelectStreamsInternal();

  void SubInterface(SelectSubType action);
  void DisconnectCurrentSubtitlePins();

  /*!
   * \brief Find the subtitle files beside what is playing and offer them alongside its tracks
   *
   * Only for the graphs a subtitle filter is in, which is all of them: the internal subtitle
   * manager stands down whenever one is loaded, and it was the only thing that ever went
   * looking for a file.
   */
  void LoadExternalSubtitles();

  //! What LoadStreams was told is playing, since the application cannot yet say
  std::string m_currentFile;

  std::vector<CDSStreamDetailAudio *> m_audioStreams;
  std::vector<CDSStreamDetailSubfilter *> m_subfilterStreams;
  std::vector<CDSStreamDetailSubfilter *> m_subfilterStreams_int;
  std::vector<CDSStreamDetailEdition *> m_editionStreams;
  bool m_mkveditions;
  Com::SComPtr<IDirectVobSub> m_pIDirectVobSub;
  Com::SComPtr<ILAVAudioSettings> m_pILAVAudioSettings;
  Com::SComPtr<IAMStreamSelect> m_pIAMStreamSelect;
  Com::SComPtr<IAMStreamSelect> m_pIAMStreamSelectSub;
  Com::SComPtr<IFilterGraph2> m_pGraphBuilder;
  Com::SComPtr<IBaseFilter> m_pSplitter;
  Com::SComPtr<IBaseFilter> m_pSubs;

  bool m_bSubfilterVisible;
  bool m_bHasSubsFilter;
  bool m_init;
  bool m_bIsXYVSFilter;
  //! \brief Whether the viewer's subtitle settings are being watched for changes
  bool m_bWatchingSubtitleStyle = false;
  bool m_bIsLavAudio;
  bool m_bIsFFDSAudio;
  float m_InitialAudioDelay;
  float m_InitialSubsDelay;
  int m_lastDelay;
  int m_iCurrentSubtitleStream;

  CDSStreamDetailVideo m_videoStream;
  CCriticalSection m_lock;
  bool m_dvdStreamLoaded;

  CEvent m_readyEvent;
};

class CSubtitleManager
{
public:
  CSubtitleManager(CStreamsManager* pStreamManager);
  ~CSubtitleManager();

  void Initialize();
  void Unload();
  bool Ready();

  /** @remark renderRect represent the rendering surface size */
  //HRESULT GetTexture(Com::SComPtr<IDirect3DTexture9>& pTexture, Com::SmartRect& pSrc, Com::SmartRect& pDest, Com::SmartRect& renderRect);
  HRESULT AlphaBlt(ID3D11DeviceContext1* pDevContext, Com::SmartRect& pSrc, Com::SmartRect& pDest, Com::SmartRect& renderRect);

  void StopThread();
  void StartThread();

  /// @return A std::vector of all subtitle streams (internal or external) found in the media file
  std::vector<CDSStreamDetailSubtitle *>& GetSubtitles();
  /// @return Subtitles count
  int  GetSubtitleCount();
  /// @return The index of the current subtitle
  int  GetSubtitle();
  /** Get the displayname of a subtitle
   * @param[in] iStream Index of the subtitle to get displayname of
   * @param[out] strStreamName Name of the iStream subtitle
   */
  void GetSubtitleName(int iStream, std::string &strStreamName, std::string& strStreamLang);
  /** Change the current subtitle
   * @param[in] iStream Index of the subtitle
   * @remarks If the IAMStreamSelect interface wasn't found, the graph must be stopped and restarted in order to change the subtitle
   */
  void SetSubtitle(int iStream);
  /// @return True if subtitles are visible, false otherwise
  bool GetSubtitleVisible();
  /** Set subtitle visibility
   * @param[in] bVisible Subtitle visibility
   */
  void SetSubtitleVisible(bool bVisible);
  /** Set subtitle delay
   * @param in fValue Subtitle delay (in ms)
   */
  void SetSubtitleDelay(float fValue = 0.0f);
  /// @return Current subtitle delay
  float GetSubtitleDelay(void);
  /** Add a subtitle to the manager
   * @param[in] subFilePath Path of the subtitle
   * @return -1 if the function fails. Otherwise, returns the index of the added subtitle
   * @remarks The subtitle will be automatically flagged as external
   */
  int AddSubtitle(const std::string& subFilePath);

  void SetTime(REFERENCE_TIME rtNow);
  void SetSizes(Com::SmartRect window, Com::SmartRect video);

  CDSStreamDetailSubtitle* GetSubtitleStreamDetail(unsigned int iIndex = 0);
  CDSStreamDetailSubtitleExternal* GetExternalSubtitleStreamDetail(unsigned int iIndex = 0);

  void SetTimePerFrame(REFERENCE_TIME iTimePerFrame);

  void SelectBestSubtitle();

private:
  void DisconnectCurrentSubtitlePins(void);
  IPin *GetFirstSubtitlePin(void);
  static void InternalDeleteSubtitleManager(ISubManager* pManager);

  std::vector<CDSStreamDetailSubtitle *> m_subtitleStreams;
  std::shared_ptr<ISubManager> m_pManager;
  std::shared_ptr<ILogImpl> m_Log;
  CStreamsManager* m_pStreamManager;
  bool m_bSubtitlesUnconnected;
  bool m_bSubtitlesVisible;
  AM_MEDIA_TYPE m_subtitleMediaType;
  REFERENCE_TIME m_rtSubtitleDelay;
};
