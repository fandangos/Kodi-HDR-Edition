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

#include <atomic>
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
   * \brief The disc being navigated, opened if it is not already
   * \return The running disc, or nullptr if it cannot be navigated
   *
   * The disc outlives the filter graph on purpose. When the disc moves from its menu to a
   * title the graph has to be built again for the new programme, and the disc must carry
   * on where it was: rewinding it to first play would take the viewer back to the menu
   * they just chose from, and a BD-J disc would lose everything its menu was running.
   */
  static std::shared_ptr<CDSBlurayNavigator> Session(const std::string& path);

  //! \brief Finish with the disc, when playback really ends rather than between graphs
  static void EndSession();

  /*!
   * \brief Start reporting when the disc moves to another programme
   *
   * Called once a graph has been built on what the disc is playing. Before that the disc is
   * still finding its way in and there is no player to tell, so changes are only remembered.
   */
  void AnnounceProgrammeChanges();

  /*!
   * rief Stop reporting changes while the player is opening the disc again
   *
   * A disc on its way from a menu to a title passes through short playlists. Each is a
   * change, and reporting them while an open is already under way asks for the disc to be
   * opened again on top of itself.
   */
  void SuspendProgrammeChanges() { m_announceChanges = false; }

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

  /*!
   * \brief The playlist being played, which changes as the disc moves around
   *
   * Bytes either side of a change belong to different programmes. Handing both to one
   * demuxer as though they were one file gives it something it cannot parse.
   */
  uint32_t Playlist() const;

  //! \brief Whether the disc has arrived at its top menu, which is where it was heading
  bool InMainMenu() const;

  /*!
   * rief Whether the disc currently has menu graphics on screen
   *
   * The honest answer to "is the viewer looking at a menu", and the one to use for deciding
   * where their key presses should go. It covers a popup menu over a playing film exactly
   * as it covers a top menu, because both are the disc asking for something to be drawn.
   * libbluray's own answer is about the title's menu code being loaded, which stays true
   * for the whole of a BD-J film and so is no use for this.
   */
  bool MenuOnScreen() const { return m_menuOnScreen; }

  /*!
   * rief Take the menu off the screen
   *
   * The disc clears its own menus by sending an empty overlay, but it only does that while
   * it is being read. When a playlist is taken away and played directly the disc stops
   * being read mid-menu, so the last thing it asked for would otherwise stay drawn over the
   * film, and the player would go on believing a menu was up.
   */
  void ClearMenu();

  /*!
   * \brief Give up on a disc that is not going to produce anything
   *
   * A disc whose menu failed to start returns nothing but idle events for as long as it is
   * asked, and the read that is asking never comes back on its own. Safe to call from
   * another thread, which is the only way it is any use.
   */
  void Abort();

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
  /*!
   * \brief Whether a menu is on screen
   *
   * Answered from a cached flag rather than by asking the disc. The interface polls this
   * constantly, and the disc is held by whichever read is in progress, so asking directly
   * would leave the interface waiting on disc reads.
   */
  bool IsInMenu() const { return m_inMenu; }
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
  /*!
   * \brief Notice when the disc has moved to a different programme
   *
   * Checked on every event, because the change happens when the viewer chooses something,
   * which is the thread carrying the keypress rather than the one reading the disc.
   */
  void NotePlaylist();

  //! \brief Let the disc out of a hold so bytes keep flowing, see Read()
  bool ReleaseHold();

  //! \brief Leave a still early, which is what choosing something from a menu does
  void EndStill();

  /*!
   * \brief The running disc, safe to call into from any thread
   *
   * Menu commands must never wait on the thread reading the disc. A read can sit inside
   * libbluray indefinitely -- a disc whose menu failed to start returns nothing but idle
   * events forever -- and anything queued behind it waits just as long. libbluray locks
   * itself for user input, which is how Kodi's own player drives menus from the interface
   * while its reader is mid-read, so no lock of ours belongs in the way. Taking a copy of
   * the pointer keeps the disc alive for the duration of the call and nothing more.
   */
  std::shared_ptr<CDVDInputStreamBluray> Input() const;

  mutable CCriticalSection m_lock;
  //! Guards only assignment and copying of m_input, never a call into it
  mutable CCriticalSection m_inputLock;
  std::shared_ptr<CDVDInputStreamBluray> m_input;
  int64_t m_produced{0};
  std::atomic<bool> m_finished{false};

  //! Kept in step by the thread reading the disc, so the interface can ask without taking
  //! the lock that reads are holding
  std::atomic<bool> m_inMenu{false};
  //! Last playlist noticed, so a change is announced once
  uint32_t m_playlistSeen{0};
  std::atomic<bool> m_announceChanges{false};

  //! A still is the disc holding one picture and sending nothing. Only menus drawn over a
  //! fixed background arrive this way: a menu with moving video behind it is an ordinary
  //! playlist that loops, and its data flows like anything else. Discs also use timed
  //! stills to pause between pieces of the opening sequence, and those expire on their own.
  bool m_still{false};
  bool m_stillIsIndefinite{false};
  std::chrono::steady_clock::time_point m_stillUntil{};

  //! Most recent overlay the disc asked to be drawn, waiting to be picked up by the
  //! renderer. Guarded separately from the disc itself, see TakeOverlay.
  mutable CCriticalSection m_overlayLock;
  std::shared_ptr<CDVDOverlayGroup> m_overlay;
  bool m_overlayPending{false};
  //! Whether the last thing the disc asked to be drawn was a menu rather than nothing, see
  //! MenuOnScreen. Read from the GUI thread, written from the disc's, hence atomic.
  std::atomic<bool> m_menuOnScreen{false};
  uint64_t m_overlayCount{0};

  static CDSBlurayNavigator* m_instance;

  //! The disc kept between graphs, see Session()
  static std::shared_ptr<CDSBlurayNavigator> m_session;
  static std::string m_sessionPath;
};

#endif
