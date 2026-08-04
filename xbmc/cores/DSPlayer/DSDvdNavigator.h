/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

//
// Drives a DVD's own navigation for DSPlayer.
//
// The same shape as CDSBlurayNavigator, and for the same reason: Kodi already contains a
// complete DVD navigation engine in CDVDInputStreamNavigator -- first play, menus, button
// highlights, angles, and an ISO read through Kodi's own virtual file system so a disc image
// on a network share works without being mounted. It is tied to VideoPlayer only through
// IVideoPlayer, which is two methods, so it is reused as-is by implementing them here.
//
// What is different from Blu-ray is what a hold means.
//
// libdvdnav does not just hand over bytes: at a change of video title set, at a
// discontinuity, and at a still it stops and asks the player to flush its demuxer before
// going on. VideoPlayer answers by throwing its demuxer away and opening a new one.
// DSPlayer hands a byte stream to a DirectShow splitter that owns the demuxer itself and
// cannot be flushed, so those holds are cleared here and the bytes kept flowing -- exactly
// what CDSBlurayNavigator::ReleaseHold does for the Blu-ray case. A change of title set is
// a genuinely different programme, and that is reported to the player so the graph can be
// rebuilt on it, which is the path the Blu-ray work already established.
//

#pragma once

#if HAS_DS_PLAYER

#include "IDSDiscNavigator.h"
#include "cores/VideoPlayer/DVDDemuxSPU.h"
#include "cores/VideoPlayer/IVideoPlayer.h"
#include "threads/CriticalSection.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

class CDVDInputStreamNavigator;
class CDVDOverlayGroup;
class CDVDOverlaySpu;

/*!
 * \brief Plays a DVD through its own menus and navigation commands
 *
 * Only one of these exists while a disc is playing. The DirectShow source filter pulls bytes
 * from it on the graph's streaming thread while menu input arrives from the GUI thread, so
 * the entry points that make the disc act take the same lock: libdvdnav is not thread safe.
 */
class CDSDvdNavigator : public IVideoPlayer, public IDSDiscNavigator
{
public:
  CDSDvdNavigator();
  ~CDSDvdNavigator() override;

  /*!
   * \brief The disc currently being navigated, or nullptr
   *
   * The renderer and the input handling both need to reach the running disc, and neither has
   * a path to the source filter that owns it. Only one disc plays at a time.
   */
  static CDSDvdNavigator* Get() { return m_instance; }

  /*!
   * \brief The disc being navigated, opened if it is not already
   * \return The running disc, or nullptr if it cannot be navigated
   *
   * The disc outlives the filter graph on purpose. When the disc moves from its menu to a
   * title the graph has to be built again for the new programme, and the disc must carry on
   * where it was: rewinding it to first play would take the viewer back to the menu they
   * just chose from.
   */
  static std::shared_ptr<CDSDvdNavigator> Session(const std::string& path);

  //! \brief Finish with the disc, when playback really ends rather than between graphs
  static void EndSession();

  /*!
   * \brief Start reporting when the disc moves to another programme
   *
   * Called once a graph has been built on what the disc is playing. Before that the disc is
   * still finding its way in and there is no player to tell, so changes are only remembered.
   */
  void AnnounceProgrammeChanges();

  //! \brief Stop reporting changes while the player is opening the disc again
  void SuspendProgrammeChanges() override { m_announceChanges = false; }

  bool Open(const std::string& path);
  void Close();
  bool IsOpen() const { return m_input != nullptr; }

  /*!
   * \brief Read the next bytes the disc wants played
   * \return Bytes read, 0 when there is nothing to read just now, negative on error
   *
   * Reads are sequential and cannot be rewound: what comes back depends on where the disc's
   * state machine currently is, not on any file position. Nothing to read is the normal state
   * while a menu waits for the viewer, so it does not mean the disc is over. Ask Finished().
   *
   * \note libdvdnav deals in whole 2048 byte blocks and refuses a smaller buffer than one.
   */
  int Read(uint8_t* buffer, int size);

  //! \brief Whether the disc has played everything it intends to
  bool Finished() const override { return m_finished; }

  /*!
   * \brief Which programme the disc is playing, as title and video title set
   *
   * Bytes either side of a change belong to different programmes. Handing both to one
   * demuxer as though they were one file gives it something it cannot parse -- on a DVD the
   * two may differ in resolution and aspect as well as in content.
   * \{
   */
  int Title() const;
  int Chapter() const;
  /*! \} */

  /*!
   * \brief How many times the disc has moved to another cell
   *
   * The finest boundary the disc announces, and the only one that is any use for deciding
   * where a presented stream may begin. A title number is far too coarse: everything the disc
   * shows in its menu domain is title 0, menu after menu, and the opening bytes of a DVD menu
   * cell are the only I-frame it has -- start the stream one cell late and LAV Splitter finds
   * audio and subpicture and no video at all, so madVR is never configured and the screen
   * stays black while the position happily advances.
   *
   * A count rather than an identity, because what matters is only that it changed.
   */
  uint32_t Cell() const { return m_cellsSeen; }

  //! \brief How long the title the disc is on runs for, in milliseconds, 0 when unknown
  int TitleDuration() const;

  /*!
   * \brief Whether the disc currently has a menu on screen
   *
   * Unlike Blu-ray, the disc's own answer is the honest one here and is used as it stands.
   * libdvdnav says yes when the NAV packet carries buttons or when the disc is outside the
   * title domain, which is precisely "the viewer is looking at something to choose from".
   * The Blu-ray trap that made this question so expensive -- libbluray answering about menu
   * code being loaded, which stays true for a whole BD-J film -- has no equivalent.
   */
  bool MenuOnScreen() const override { return m_inMenu; }

  /*!
   * \brief Whether the disc itself says its menu is visible
   *
   * The same answer as MenuOnScreen, and deliberately so. On Blu-ray these are two genuinely
   * different questions -- what the disc has drawn against what libbluray believes -- and
   * their disagreeing is a diagnosis. On a DVD there is only ever one answer, and pretending
   * otherwise would put a second opinion in the log that was never independent.
   */
  bool DiscSaysMenuVisible() const override { return m_inMenu; }

  /*!
   * \brief Whether the disc is holding one picture until the viewer chooses something
   *
   * An indefinite still is the disc promising that not one more byte is coming. Anything
   * waiting on the disc for bytes has to ask this first, or it waits for as long as the
   * viewer takes -- which, on the thread the splitter reads with, means the graph is never
   * built and nothing is ever shown. A timed still expires on its own.
   */
  bool HoldingIndefiniteStill() const { return m_still && m_stillIsIndefinite; }

  //! \brief Give up on a disc that is not going to produce anything
  void Abort();

  //! \brief Total bytes handed out so far, for reporting how much of the stream exists
  int64_t Produced() const { return m_produced; }

  /*! \name Menu control, called from the GUI thread
   *  Expressed in player terms rather than libdvdnav calls, so the input handling does not
   *  need to know which disc format is playing.
   *  \{
   */
  bool ShowMenu() override;
  void OnBack() override;
  void OnUp() override;
  void OnDown() override;
  void OnLeft() override;
  void OnRight() override;
  void OnSelect() override;
  bool IsInMenu() const { return m_inMenu; }
  /*! \} */

  /*!
   * \brief The running disc, for the parts of DSPlayer that ask it about streams
   *
   * Audio tracks, subtitle tracks and chapters are all the input stream's to answer, and
   * wrapping each one here would be a layer with nothing in it.
   */
  std::shared_ptr<CDVDInputStreamNavigator> Input() const;

  /*!
   * \brief Collect a menu overlay the disc has produced since the last call
   * \param overlay Receives the overlay, which may be empty to clear the screen
   * \return true when a new overlay arrived, false when what is on screen still stands
   *
   * Called from the render thread while reads run on the graph's streaming thread, so the
   * handover has its own lock: waiting on the read lock would stall rendering for as long as
   * a disc read takes.
   */
  bool TakeOverlay(std::shared_ptr<CDVDOverlayGroup>& overlay);

  /*!
   * \brief The size of the picture the disc composes its menu against
   *
   * A DVD's subpicture is laid out in the coordinates of its own video, 720x480 or 720x576,
   * not in the coordinates of whatever the picture is finally scaled to. The renderer needs
   * both to place a button where the viewer sees it.
   */
  void MenuPlaneSize(int& width, int& height) const;

  // IVideoPlayer
  int OnDiscNavResult(void* pData, int iMessage) override;
  void GetVideoResolution(unsigned int& width, unsigned int& height) override;

private:
  /*!
   * \brief Pick the disc's subpicture packets out of the blocks going past
   *
   * A DVD's menu is drawn by its subpicture stream, and that stream is inside the very same
   * program stream the splitter is being handed -- there is no separate graphics plane as
   * there is on a Blu-ray. The splitter does expose it as a subtitle track, but nothing
   * downstream can know which button is selected, because that lives in the NAV packets and
   * only libdvdnav reads those. So the packets are decoded here as well, and the menu is
   * drawn through the same overlay path a Blu-ray's is.
   */
  void CollectSubpicture(const uint8_t* block, size_t length);

  /*!
   * \brief Put the currently selected button's colours onto the menu picture and publish it
   *
   * The picture and the highlight arrive separately: the picture comes from the stream, the
   * highlight from the NAV packet the disc is on. Either changing means the menu has to be
   * drawn again.
   */
  void PublishMenu();

  //! \brief Take the menu off the screen
  void ClearMenu();

  /*!
   * \brief Move the selection and say what the disc made of it
   *
   * All four directions do the same thing to a different libdvdnav call, and what is worth
   * knowing is the same for each: whether the disc's own selected button actually moved.
   */
  void MoveSelection(const char* what, void (CDVDInputStreamNavigator::*move)());

  //! \brief Notice when the disc has moved to a different programme
  void NoteTitle();

  //! \brief Let the disc out of a hold so bytes keep flowing, see Read()
  bool ReleaseHold();

  //! \brief Leave a still early, which is what choosing something from a menu does
  void EndStill();

  //! Guards only assignment and copying of m_input, never a call into it
  mutable CCriticalSection m_inputLock;
  std::shared_ptr<CDVDInputStreamNavigator> m_input;

  /*!
   * Held around anything that makes the disc act -- the viewer's key presses. Never held
   * around a read: a read can sit inside libdvdnav for as long as the disc feels like giving
   * nothing, and a key press queued behind one waits exactly that long.
   */
  mutable CCriticalSection m_discLock;

  int64_t m_produced{0};
  std::atomic<bool> m_finished{false};

  //! Kept in step by the thread reading the disc, so the interface can ask without waiting
  //! on a read
  std::atomic<bool> m_inMenu{false};

  //! Last title noticed, so a change is announced once. Noticed from the reading thread and
  //! from the thread carrying a key press, hence atomic.
  std::atomic<int> m_titleSeen{-1};
  std::atomic<bool> m_announceChanges{false};

  //! Counts DVDNAV_CELL_CHANGE, see Cell(). Written from the reading thread and read by the
  //! source filter deciding where a stream may begin, hence atomic.
  std::atomic<uint32_t> m_cellsSeen{0};

  //! Decodes the disc's subpicture packets into menu pictures. Only ever touched from the
  //! thread reading the disc.
  CDVDDemuxSPU m_spu;
  //! The menu picture last decoded, kept because the highlight changes without it changing
  std::shared_ptr<CDVDOverlaySpu> m_menu;
  //! The button the highlight was last drawn for, so an unchanged menu is not redrawn
  int m_buttonDrawn{-1};

  //! Most recent menu the disc asked to be drawn, waiting to be picked up by the renderer
  mutable CCriticalSection m_overlayLock;
  std::shared_ptr<CDVDOverlayGroup> m_overlay;
  bool m_overlayPending{false};
  uint64_t m_overlayCount{0};

  //! A still is the disc holding one picture and sending nothing, which is how a menu drawn
  //! over a fixed background waits. Set on the thread reading the disc and read by whoever is
  //! waiting for its bytes, hence atomic.
  std::atomic<bool> m_still{false};
  std::atomic<bool> m_stillIsIndefinite{false};
  std::chrono::steady_clock::time_point m_stillUntil{};

  static CDSDvdNavigator* m_instance;

  //! The disc kept between graphs, see Session()
  static std::shared_ptr<CDSDvdNavigator> m_session;
  static std::string m_sessionPath;
};

#endif
