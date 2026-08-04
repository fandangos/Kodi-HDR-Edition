/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

//
// Source filter that reads a DVD through libdvdnav.
//
// The same shape as the Blu-ray source next door, and built for the same reasons. Reading
// the disc through Kodi rather than letting a DirectShow filter open the file means a disc
// image on a network share works without being mounted, the disc's own navigation decides
// what plays, and CSS is handled by the libdvdcss Kodi already ships.
//
// There is only one mode here, where the Blu-ray source has two. A Blu-ray title can be
// opened a second time on a handle of its own and read as an ordinary seekable file, which
// is what gives a feature a real length. libdvdnav has no equivalent: everything -- menus,
// first play, the feature -- comes out of the one navigation session, sequentially. So the
// stream presented to the splitter is always the disc's own output, and recently delivered
// bytes are kept in a window that short backward reads are served from.
//
// What that costs, and what is done about it, is the same list the Blu-ray work paid for:
// a disc only advances while it is read, a short read means end of file to DirectShow, and
// a menu holding a still picture must block a read rather than end the stream.
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

class CDSDvdNavigator;

//! Name this filter is registered and selected by, see FilterCoreFactory and
//! mediasconfig.xml. Like the Blu-ray source and unlike every other source filter, this one
//! is given Kodi's own path rather than a Windows one, because the disc is read through
//! Kodi's virtual file system.
constexpr const char* INTERNAL_DVD_SOURCE = "internal_dvdsource";

/*!
 * \brief Exposes a DVD as a byte stream the splitter can demux
 */
class CDSDvdStream : public CAsyncStream
{
public:
  CDSDvdStream() = default;
  ~CDSDvdStream() override;

  /*!
   * \brief Open a disc and let it run its own navigation
   * \param path Path to a disc image, a VIDEO_TS folder, or VIDEO_TS.IFO
   * \return S_OK when the disc can be read
   */
  HRESULT Open(const std::string& path);

  //! \brief The running navigation session, or nullptr
  CDSDvdNavigator* Navigator() const { return m_navigator.get(); }

  /*!
   * \brief Stop feeding the graph so it can be taken down
   *
   * Taking a filter graph down waits for its streaming threads to finish, and those threads
   * are reading from here. A read waiting on the disc keeps the graph alive for as long as
   * the disc stays quiet, which on a menu is indefinitely.
   */
  static void StopFeedingTheGraph();

  /*!
   * \brief Keep the disc moving although the splitter has stopped reading
   *
   * A programme's end is not the disc's end. The splitter stops reading when the programme it
   * parsed runs out, and the disc only advances while it is read.
   */
  static void FollowTheDisc();

  /*!
   * \brief Let the disc run on for a moment, because the viewer has just told it to do something
   *
   * A disc only acts on a button as it is read, and it is read no faster than the splitter
   * consumes -- so a button press has to wait for everything already queued ahead of it to
   * play out first. Measured on a DVD menu: choosing "play" took fifteen seconds to reach the
   * feature, with the disc ticking along at the four megabits a second the menu plays at.
   *
   * Reading flat out for a moment afterwards costs nothing, because whatever is read between
   * the press and the disc moving is thrown away by the rebuild that follows. Bounded, so a
   * button that turns out to lead nowhere does not leave the disc racing round a looping menu.
   */
  static void LetTheDiscRunOn();

  /*!
   * \brief The graph is built, so the disc may be read on past its opening bytes
   *
   * A splitter reads forward through the stream while it works out what is in it, then goes
   * back to the beginning to play it. A navigated disc cannot be rewound, so it is held near
   * its start until the splitter stops scanning. The hold is expressed as a short read, which
   * is how this interface says the stream has ended, so it must be lifted the moment the
   * splitter starts demuxing -- see the Blu-ray source for what leaving it on costs.
   * \{
   */
  static void NoteGraphBuilt();
  static void NoteGraphRunning();
  /*! \} */

  // CAsyncStream
  HRESULT SetPointer(LONGLONG llPos) override;
  HRESULT Read(PBYTE pbBuffer, DWORD dwBytesToRead, BOOL bAlign, LPDWORD pdwBytesRead) override;
  LONGLONG Size(LONGLONG* pSizeAvailable = nullptr) override;
  DWORD Alignment() override;
  void Lock() override;
  void Unlock() override;

private:
  void Close();

  //! \brief Start the disc and read it until it settles on something worth showing
  bool OpenNavigation(const std::string& path);

  /*!
   * \brief Pull the next bytes from the disc into the history window
   * \return Bytes added, 0 when the disc has nothing to give just now
   *
   * Reads block by block, because that is what libdvdnav hands over and because it is the
   * only granularity at which a change of programme can be cut cleanly -- see m_settling.
   */
  DWORD Pull();

  //! \brief Add bytes to the history window, oldest falling out of the far end
  void Append(const uint8_t* bytes, size_t length);

  /*!
   * \brief Keep the ring full, on a thread of its own
   *
   * Reads must never wait on the disc. A splitter asked for the length of a stream holds its
   * own locks while it asks, and the application thread asks for the playing time every
   * frame, so a read that waits on a disc showing a quiet menu freezes the whole interface.
   */
  void Produce();

  //! \brief Throw away what has been played and start the stream again from nothing
  void Discard();

  //! \brief Copy out of the history window, which must already cover the range
  void CopyFromHistory(LONGLONG position, PBYTE buffer, DWORD length) const;

  //! \brief Periodically log throughput of the disc reads
  void ReportThroughput(DWORD bytesRead, double readMilliseconds);

  CCriticalSection m_lock;
  std::string m_path;

  //! Position the reader asked for
  LONGLONG m_position{0};

  std::shared_ptr<CDSDvdNavigator> m_navigator;

  //! Guards the history window alone, so that waiting for bytes never holds up anything that
  //! only wants to know how long the stream is
  std::mutex m_ringMutex;
  std::condition_variable m_ringGrew;
  std::thread m_producer;
  std::atomic<bool> m_producerStop{false};
  //! Read the disc onwards even though the splitter is no longer keeping up
  std::atomic<bool> m_followDisc{false};
  //! Until when the readahead cap is ignored because the viewer pressed something, as a
  //! steady_clock tick count so it can be set from the thread carrying the key press and read
  //! by the producer, see LetTheDiscRunOn
  std::atomic<long long> m_runOnUntil{0};
  //! Whether the disc is still being held near its opening bytes, see NoteGraphBuilt
  std::atomic<bool> m_holdingOpeningBytes{true};
  //! The stream currently feeding the graph, so it can be stopped before teardown
  static CDSDvdStream* m_feeding;

  //! The bytes most recently produced, oldest overwritten first. Navigation cannot rewind,
  //! so this is the only thing a backward read can be answered from.
  std::vector<uint8_t> m_history;
  size_t m_historyBytes{0};
  size_t m_historyEnd{0};

  /*!
   * Total bytes the disc has produced, which is also where the history window ends.
   *
   * Atomic because Size() is asked from another thread while a read is in progress, and a
   * read can sit inside libdvdnav for as long as the disc has nothing to give.
   */
  std::atomic<LONGLONG> m_produced{0};
  std::atomic<bool> m_exhausted{false};

  //! The title the presented stream belongs to. Anything the disc plays after moving off it
  //! is a different programme and does not belong in the same stream.
  int m_title{0};

  /*!
   * Whether the disc is still being read to find out where it is going, in which case what
   * it passes through is thrown away and only what it settles on is kept.
   *
   * While this is set, Pull cuts the stream at the exact block the disc changes programme on.
   * Cutting anywhere coarser takes the new programme's opening bytes with the old one's --
   * and on a DVD menu those opening bytes are the only I-frame there is, so LAV Splitter then
   * finds an audio stream and no video at all.
   */
  bool m_settling{false};
  //! The cell Pull is currently gathering, while settling. A count rather than an identity,
  //! see CDSDvdNavigator::Cell.
  uint32_t m_collecting{0};
  /*!
   * Whether a picture has turned up in what has been collected since the last discard.
   *
   * The splitter exposes no video pin unless it finds one, and a DVD menu is very often a
   * single I-frame at the head of its cell with a minute of audio over it -- so a stream
   * handed over a moment too late plays perfectly and shows nothing at all.
   */
  bool m_haveVideo{false};
  //! Counts the opening reads, which are traced so the exchange that decides whether the
  //! splitter will play the stream can be read back afterwards
  uint32_t m_traced{0};

  // Throughput accounting, reset each time a summary is logged
  std::chrono::steady_clock::time_point m_statsSince{};
  uint64_t m_statsBytes{0};
  uint64_t m_statsReads{0};
  double m_statsSlowestRead{0.0};
};

/*!
 * \brief DVD source filter, wraps CDSDvdStream in the async reader boilerplate
 */
class __declspec(uuid("6C3F1B84-1E70-4A2D-9C55-2B1A0F5E7D33")) CDSDvdReader
  : public CAsyncReader,
    public IFileSourceFilter
{
public:
  // Instantiated directly rather than through CoCreateInstance, so there is nothing to
  // register
  STDMETHODIMP Register() { return S_OK; }
  STDMETHODIMP Unregister() { return S_OK; }

  DECLARE_IUNKNOWN

  CDSDvdReader(LPUNKNOWN pUnknown, HRESULT* phr);
  ~CDSDvdReader() override = default;

  // IFileSourceFilter
  HRESULT STDMETHODCALLTYPE Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt) override;
  HRESULT STDMETHODCALLTYPE GetCurFile(LPOLESTR* ppszFileName, AM_MEDIA_TYPE* pmt) override
  {
    return E_NOTIMPL;
  }

  STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

private:
  CDSDvdStream m_stream;
};
