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

#include <chrono>
#include <memory>
#include <string>
#include <vector>

struct bluray;
class CDSBlurayNavigator;

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
   * \return true when the disc is running its own menus
   */
  bool OpenNavigation(const std::string& path);

  /*!
   * \brief Select a title and read it directly, for discs that cannot be navigated
   */
  HRESULT OpenTitle(const std::string& path);

  HRESULT ReadNavigation(PBYTE pbBuffer, DWORD dwBytesToRead, LPDWORD pdwBytesRead);
  HRESULT ReadTitle(PBYTE pbBuffer, DWORD dwBytesToRead, LPDWORD pdwBytesRead);

  /*!
   * \brief Pull the next bytes from the disc into the history window
   * \return Bytes added, 0 when the disc has no more to give
   */
  DWORD Pull();

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
  std::unique_ptr<CDSBlurayNavigator> m_navigator;
  //! The bytes most recently produced, oldest overwritten first. Navigation cannot rewind,
  //! so this is the only thing a backward read can be answered from.
  std::vector<uint8_t> m_history;
  size_t m_historyBytes{0};
  size_t m_historyEnd{0};
  //! Total bytes the disc has produced, which is also where the history window ends
  LONGLONG m_produced{0};
  bool m_exhausted{false};

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
