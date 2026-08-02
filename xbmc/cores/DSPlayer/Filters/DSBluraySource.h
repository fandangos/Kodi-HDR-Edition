/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

//
// Source filter that reads a Blu-ray disc through libbluray.
//
// The LAV Splitter Source opens disc images itself, but it picks a title on its own and
// fails outright on some discs. Reading through libbluray instead gives us the disc's own
// title list and works on discs LAV cannot open.
//
// The filter reads a disc one of two ways.
//
// In navigation mode the disc runs its own menus and decides what plays, through
// CDSBlurayNavigator. That produces a strictly sequential stream, which does not fit the
// random access interface a splitter expects, so recently delivered bytes are kept in a
// window that short backward reads are served from. See CDSBlurayStream::ReadNavigation.
//
// In title mode a single title is selected up front and exposed as an ordinary seekable
// byte stream. This is what a disc without usable menus falls back to.
//
// Either way the LAV Splitter demuxes the result downstream exactly as it would a file.
//

#pragma once

#include "filters/asyncio.h"
#include "filters/asyncrdr.h"
#include "threads/CriticalSection.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct bluray;
class CDSBlurayNavigator;

//! Name this filter is registered and selected by, see FilterCoreFactory and
//! mediasconfig.xml. Unlike every other source filter this one is given Kodi's own path
//! rather than a Windows one, because navigating a disc reads it through Kodi's virtual
//! file system, which is the only thing that can reach a network share the way Kodi does.
constexpr const char* INTERNAL_BLURAY_SOURCE = "internal_bluraysource";

/*!
 * \brief Exposes a Blu-ray as a byte stream the splitter can demux
 */
class CDSBlurayStream : public CAsyncStream
{
public:
  CDSBlurayStream() = default;
  ~CDSBlurayStream() override;

  /*!
   * \brief Open a disc, preferring its own menus
   * \param path Path to a disc image or a disc folder
   * \return S_OK when the disc can be read
   */
  HRESULT Open(const std::string& path);

  //! \brief The running navigation session, or nullptr when playing a title directly
  CDSBlurayNavigator* Navigator() const { return m_navigator.get(); }

  /*!
   * \brief Stop feeding the graph so it can be taken down
   *
   * Taking a filter graph down waits for its streaming threads to finish, and those threads
   * are reading from here. A read waiting on the disc keeps the graph alive for as long as
   * the disc stays quiet, which on a menu is indefinitely, so the graph has to be told there
   * is nothing more coming before it is stopped.
   */
  static void StopFeedingTheGraph();

  /*!
   * rief Keep the disc moving although the splitter has stopped reading
   *
   * A programme's end is not the disc's end. The splitter stops reading when the programme
   * it parsed runs out, and the disc only advances while it is read, so somebody has to
   * keep reading it through to whatever it plays next.
   */
  static void FollowTheDisc();

  /*!
   * rief The graph is built, so the disc may be read on past its opening bytes
   *
   * A splitter reads forward through the stream while it works out what is in it, and then
   * goes back to the beginning to play it. A navigated disc cannot be rewound, so the
   * beginning has to still be in the window when it comes back for it, and until the splitter
   * has finished scanning the disc is held near its start however far ahead it reads.
   *
   * The hold is expressed to the splitter as a short read, which is how this interface says
   * the stream has ended -- so it has to be lifted the moment the splitter stops scanning and
   * starts demuxing, which is when the graph is built. From then on the only thing keeping the
   * opening bytes reachable is READAHEAD, which holds the disc close behind the splitter
   * anyway, and a rewind further back than the window reaches would say so in the log.
   */
  static void NoteGraphBuilt();

  /*!
   * rief Playback has started
   *
   * Lifts the same hold, for any path that reaches here without the graph having been
   * announced as built.
   */
  static void NoteGraphRunning();

  /*!
   * rief Give a stranded playlist back to the disc's own navigation
   * eturn true when the disc was stranded and the graph should be rebuilt
   *
   * A playlist taken away and played directly comes to an end, and the disc that chose it is
   * still sitting exactly where it was: a disc only moves while it is read, and in this mode
   * nothing reads it. It can never reach whatever it was leading to. Played through
   * navigation instead, reading it is what carries the disc on.
   */
  static bool HandBackToTheDisc();

  /*!
   * rief Whether what is playing is a playlist read directly, rather than the disc's own
   *      navigation output
   *
   * The disc's navigation session is kept alive while a feature plays, holding the menu it
   * was left in, so asking the disc whether it is in a menu says yes throughout the film.
   * Anything deciding whether the viewer is looking at a menu has to ask this instead.
   */
  static bool PlayingPlaylistDirectly() { return m_playingPlaylist; }

  // CAsyncStream
  HRESULT SetPointer(LONGLONG llPos) override;
  HRESULT Read(PBYTE pbBuffer, DWORD dwBytesToRead, BOOL bAlign, LPDWORD pdwBytesRead) override;
  LONGLONG Size(LONGLONG* pSizeAvailable = nullptr) override;
  DWORD Alignment() override;
  void Lock() override;
  void Unlock() override;

private:
  void Close();

  /*!
   * \brief Start the disc in navigation mode
   * \param following A playlist the caller has already found the disc to be on and decided
   *                  should play, or 0 to let the disc settle first
   * \return true when the disc is running its own menus
   *
   * Settling means reading the disc until it stops changing its mind, and a disc is read far
   * faster than it plays. That is the right price for finding out where a disc is heading,
   * and the wrong one when the caller has just looked and knows: a transit clip is seconds
   * long, and settling on it would throw away most of it before a frame was shown.
   */
  bool OpenNavigation(const std::string& path, uint32_t following = 0);

  /*!
   * rief Read one playlist of the disc directly, from its beginning
   *
   * What a disc navigates to, once it leaves its menus, is an ordinary playlist. Read that
   * way it is a file like any other: it has a length, it can be seeked, and a splitter can
   * work with it. The navigation session is left running underneath, holding the disc's
   * state, so its menus can be returned to.
   */
  HRESULT OpenPlaylist(const std::string& kodiPath, uint32_t playlist);

  /*!
   * \brief Select a title and read it directly, for discs that cannot be navigated
   */
  HRESULT OpenTitle(const std::string& path);

  HRESULT ReadNavigation(PBYTE pbBuffer, DWORD dwBytesToRead, LPDWORD pdwBytesRead);
  HRESULT ReadTitle(PBYTE pbBuffer, DWORD dwBytesToRead, LPDWORD pdwBytesRead);

  /*!
   * \brief Pull the next bytes from the disc into the history window
   * \return Bytes added, 0 when the disc has no more to give
   *
   * Reads the disc without holding the ring, because the disc can take as long as it likes,
   * and takes the ring only to add what came back.
   */
  DWORD Pull();

  /*!
   * \brief Keep the ring full, on a thread of its own
   *
   * Reads must never wait on the disc. A splitter asked for the length of a stream holds
   * its own locks while it asks, and the application thread asks for the playing time every
   * frame, so a read that waits on a disc showing a quiet menu freezes the whole interface.
   * This keeps bytes ready so reads only ever copy them.
   */
  void Produce();

  //! \brief Throw away what has been played and start the stream again from nothing
  void Discard();

  //! \brief Copy out of the history window, which must already cover the range
  void CopyFromHistory(LONGLONG position, PBYTE buffer, DWORD length) const;

  /*!
   * \brief Periodically log throughput of the disc reads
   *
   * Playback smoothness cannot tell us whether this source keeps up, because the renderer
   * and, when streaming the desktop, the encoder and network sit between it and the
   * picture. These figures describe the source alone.
   */
  void ReportThroughput(DWORD bytesRead, double readMilliseconds);

  CCriticalSection m_lock;
  std::string m_path;

  //! Position the reader asked for. In title mode it is applied lazily, because bd_seek()
  //! only lands on aligned unit boundaries and the remainder has to be skipped by reading.
  LONGLONG m_position{0};

  // Navigation mode
  std::shared_ptr<CDSBlurayNavigator> m_navigator;
  //! Guards the history window alone, so that waiting for bytes never holds up anything
  //! that only wants to know how long the stream is
  std::mutex m_ringMutex;
  std::condition_variable m_ringGrew;
  std::thread m_producer;
  std::atomic<bool> m_producerStop{false};
  //! Read the disc onwards even though the splitter is no longer keeping up, see FollowTheDisc
  std::atomic<bool> m_followDisc{false};
  //! Whether the disc is still being held near its opening bytes so the splitter can go back
  //! to them while it scans, see NoteGraphBuilt
  std::atomic<bool> m_holdingOpeningBytes{true};
  //! The stream currently feeding the graph, so it can be stopped before teardown
  static CDSBlurayStream* m_feeding;
  //! Whether the stream being fed to the graph is a playlist read directly, see
  //! PlayingPlaylistDirectly
  static std::atomic<bool> m_playingPlaylist;
  //! Set when a directly played playlist strands the disc, and taken by the next Open, which
  //! then leaves that playlist to the disc's own navigation, see HandBackToTheDisc
  static std::atomic<bool> m_backToNavigation;
  //! The bytes most recently produced, oldest overwritten first. Navigation cannot rewind,
  //! so this is the only thing a backward read can be answered from.
  std::vector<uint8_t> m_history;
  size_t m_historyBytes{0};
  size_t m_historyEnd{0};
  /*!
   * Total bytes the disc has produced, which is also where the history window ends.
   *
   * Atomic because Size() is asked from another thread while a read is in progress, and a
   * read can sit inside libbluray for as long as the disc has nothing to give. Answering
   * Size() under the same lock as the read would block whoever asked for exactly that long,
   * and the interface asks constantly.
   */
  std::atomic<LONGLONG> m_produced{0};
  std::atomic<bool> m_exhausted{false};
  //! The playlist the presented stream belongs to. Anything the disc plays after moving off
  //! it is a different programme and does not belong in the same stream.
  uint32_t m_playlist{0};
  //! Asked once per graph, so a disc that changes programme twice in quick succession does
  //! not queue two rebuilds
  bool m_rebuildAsked{false};
  //! Counts the opening reads, which are traced so the exchange that decides whether the
  //! splitter will play the stream can be read back afterwards
  uint32_t m_traced{0};

  // Title mode
  struct bluray* m_bd{nullptr};
  LONGLONG m_length{0};
  //! Where libbluray actually is. Reads are overwhelmingly sequential, so comparing
  //! against this avoids a seek per read.
  LONGLONG m_bdPosition{0};

  // Throughput accounting, reset each time a summary is logged
  std::chrono::steady_clock::time_point m_statsSince{};
  uint64_t m_statsBytes{0};
  uint64_t m_statsReads{0};
  uint64_t m_statsSeeks{0};
  double m_statsSlowestRead{0.0};
};

/*!
 * \brief Blu-ray source filter, wraps CDSBlurayStream in the async reader boilerplate
 */
class __declspec(uuid("9DAC0F7A-3026-4ADC-8EF3-DD9FA3247015")) CDSBlurayReader
  : public CAsyncReader,
    public IFileSourceFilter
{
public:
  // Instantiated directly rather than through CoCreateInstance, so there is nothing to
  // register
  STDMETHODIMP Register() { return S_OK; }
  STDMETHODIMP Unregister() { return S_OK; }

  DECLARE_IUNKNOWN

  CDSBlurayReader(LPUNKNOWN pUnknown, HRESULT* phr);
  ~CDSBlurayReader() override = default;

  // IFileSourceFilter
  HRESULT STDMETHODCALLTYPE Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt) override;
  HRESULT STDMETHODCALLTYPE GetCurFile(LPOLESTR* ppszFileName, AM_MEDIA_TYPE* pmt) override
  {
    return E_NOTIMPL;
  }

  STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

private:
  CDSBlurayStream m_stream;
};
