/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

//
// Drives a Blu-ray disc's own navigation for DSPlayer.
//
// Kodi already contains a complete Blu-ray navigation engine in CDVDInputStreamBluray:
// first play, menus, both HDMV and BD-J overlays, and remote control input. It is tied to
// VideoPlayer only through IVideoPlayer, which is two methods, so it can be reused as-is
// by implementing them here rather than writing a second navigation engine against
// libbluray.
//
// The difference from VideoPlayer is what happens on the far side. VideoPlayer owns its
// demuxer and can tear it down and rebuild it whenever the disc changes what it is
// playing. DSPlayer hands a byte stream to a DirectShow splitter that owns the demuxer
// itself, so navigation has to be presented as one continuous stream. That constraint
// shapes Read() below.
//

#pragma once

#if HAS_DS_PLAYER

#include "cores/VideoPlayer/IVideoPlayer.h"
#include "threads/CriticalSection.h"

#include <chrono>
#include <memory>
#include <string>

class CDVDInputStreamBluray;
class CDVDOverlayGroup;

/*!
 * \brief Plays a Blu-ray through its own menus and navigation commands
 *
 * Only one of these exists while a disc is playing. The DirectShow source filter pulls
 * bytes from it on the graph's streaming thread, while menu input arrives from the GUI
 * thread, so every entry point takes the same lock: libbluray is not thread safe.
 */
class CDSBlurayNavigator : public IVideoPlayer
{
public:
  CDSBlurayNavigator();
  ~CDSBlurayNavigator() override;

  /*!
   * \brief The disc currently being navigated, or nullptr
   *
   * The renderer and the input handling both need to reach the running disc, and neither
   * has a path to the source filter that owns it. Only one disc plays at a time.
   */
  static CDSBlurayNavigator* Get() { return m_instance; }

  /*!
   * \brief Open a disc and start it at its first play title
   * \param path Path to a disc image or a disc folder
   * \return true when the disc opened and navigation mode is running
   *
   * Fails rather than falling back to playing a title directly, so the caller can decide
   * whether to retry without navigation.
   */
  bool Open(const std::string& path);
  void Close();
  bool IsOpen() const { return m_input != nullptr; }

  /*!
   * \brief Read the next bytes the disc wants played
   * \return Bytes read, 0 when there is nothing to read just now, negative on error
   *
   * Reads are sequential and cannot be rewound: what comes back depends on where the
   * disc's state machine currently is, not on any file position. Nothing to read is the
   * normal state while a menu waits for the viewer, so it does not mean the disc is over.
   * Ask Finished() for that.
   */
  int Read(uint8_t* buffer, int size);

  //! \brief Whether the disc has played everything it intends to
  bool Finished() const;

  //! \brief Total bytes handed out so far, for reporting how much of the stream exists
  int64_t Produced() const { return m_produced; }

  /*! \name Menu control, called from the GUI thread
   *  Deliberately expressed in player terms rather than libbluray key codes, so the input
   *  handling does not need to know which disc format is playing.
   *  \{
   */
  bool ShowMenu();
  void OnBack();
  void OnUp();
  void OnDown();
  void OnLeft();
  void OnRight();
  void OnSelect();
  bool IsInMenu();
  /*! \} */

  /*!
   * \brief Collect a menu overlay the disc has produced since the last call
   * \param overlay Receives the overlay, which may be empty to clear the screen
   * \return true when a new overlay arrived, false when what is on screen still stands
   *
   * Called from the render thread while reads run on the graph's streaming thread, so the
   * handover has its own lock: waiting on the read lock would stall rendering for as long
   * as a disc read takes.
   */
  bool TakeOverlay(std::shared_ptr<CDVDOverlayGroup>& overlay);

  // IVideoPlayer
  int OnDiscNavResult(void* pData, int iMessage) override;
  void GetVideoResolution(unsigned int& width, unsigned int& height) override;

private:
  //! \brief Let the disc out of a hold so bytes keep flowing, see Read()
  bool ReleaseHold();

  //! \brief Leave a still early, which is what choosing something from a menu does
  void EndStill();

  mutable CCriticalSection m_lock;
  std::unique_ptr<CDVDInputStreamBluray> m_input;
  int64_t m_produced{0};
  bool m_finished{false};

  //! A still is the disc holding one picture and sending nothing, which is exactly what a
  //! menu waiting for the viewer looks like. Discs also use timed stills to pause between
  //! pieces of the opening sequence, and those have to expire on their own.
  bool m_still{false};
  bool m_stillIsIndefinite{false};
  std::chrono::steady_clock::time_point m_stillUntil{};

  //! Most recent overlay the disc asked to be drawn, waiting to be picked up by the
  //! renderer. Guarded separately from the disc itself, see TakeOverlay.
  mutable CCriticalSection m_overlayLock;
  std::shared_ptr<CDVDOverlayGroup> m_overlay;
  bool m_overlayPending{false};
  uint64_t m_overlayCount{0};

  static CDSBlurayNavigator* m_instance;
};

#endif
