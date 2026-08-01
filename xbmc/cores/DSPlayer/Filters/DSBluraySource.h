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
// fails outright on some discs. Reading through libbluray instead gives us the disc's
// own title list, works on discs LAV cannot open, and is the first step towards
// presenting the disc menus, which need libbluray's navigation either way.
//
// Only the data path is implemented here: the selected title is exposed as a seekable
// MPEG-TS byte stream, which the LAV Splitter demuxes downstream exactly as it would a
// file. Navigation and menu overlays come later.
//

#pragma once

#include "filters/asyncio.h"
#include "filters/asyncrdr.h"
#include "threads/CriticalSection.h"

#include <chrono>
#include <string>

struct bluray;

/*!
 * \brief Exposes one Blu-ray title as a seekable byte stream
 */
class CDSBlurayStream : public CAsyncStream
{
public:
  CDSBlurayStream() = default;
  ~CDSBlurayStream() override;

  /*!
   * \brief Open a disc and select a title to read
   * \param path Path to a disc image or a disc folder
   * \return S_OK when a title was selected and can be read
   */
  HRESULT Open(const std::string& path);

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
   * \brief Periodically log throughput of the disc reads
   *
   * Playback smoothness cannot tell us whether this source keeps up, because the renderer
   * and, when streaming the desktop, the encoder and network sit between it and the
   * picture. These figures describe the source alone.
   */
  void ReportThroughput(DWORD bytesRead, double readMilliseconds);

  CCriticalSection m_lock;
  struct bluray* m_bd{nullptr};
  LONGLONG m_length{0};
  //! Position the reader asked for, applied lazily because bd_seek() only lands on
  //! aligned unit boundaries and the remainder has to be skipped by reading
  LONGLONG m_position{0};
  //! Where libbluray actually is. Reads are overwhelmingly sequential, so comparing
  //! against this avoids a seek per read
  LONGLONG m_bdPosition{0};
  std::string m_path;

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
