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
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct SubtitleStreamInfo;

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

  /*!
   * \brief Whether the disc is still being got as far as something worth showing
   *
   * While it is, a still with nothing to press is skipped rather than waited out. A disc's
   * way in is often a run of held frames -- this one shows two ten-second stills before its
   * menu -- and sitting through them means twenty seconds of nothing at all, because the
   * splitter cannot open a stream on the hundred kilobytes they amount to and so the graph
   * is not built and no picture is shown. Nothing is lost by skipping: there is nothing on
   * those frames to choose, and the disc goes straight on to the menu it was heading for.
   *
   * Cleared once the graph exists, after which a timed still is played out properly.
   */
  void OpeningUp(bool opening) { m_openingUp = opening; }

  /*!
   * \brief Forget what the previous programme was holding for, and take over its first bytes
   *
   * The disc outlives the graph, so a stream opened for a new programme inherits whatever the
   * last one was waiting on. A still latched from the menu the viewer has just left would have
   * the very first read refuse to read, on the grounds that the disc is waiting for a choice
   * that has already been made.
   *
   * This is also where the bytes kept back from the graph that was playing the old programme
   * are handed on, see Carry().
   */
  void ReadyForANewProgramme();

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

  /*!
   * \brief What the splitter is parsing, as one comparable value
   *
   * A DVD's programme is its title *and* its program chain, not the title alone: everything a
   * disc shows in its menu domain is title 0, so by title alone one menu page looks exactly
   * like the next. That is why moving between menus left the previous menu's video on screen
   * with only the buttons changing -- no rebuild was ever asked for, and the splitter went on
   * demuxing the programme it had already parsed.
   *
   * Cells are the wrong grain in the other direction: a feature changes cell at every chapter
   * and a looping menu changes cell every time round, and neither is a new programme.
   */
  int64_t Programme() const;
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
   * \brief Whether the menu on screen is the thing being watched
   *
   * On a DVD it always is. There is no popup: the nearest thing a DVD has is `dvdnav_menu_call`,
   * which *leaves* the title for the menu domain rather than drawing over it (see §-11), so a
   * menu being up means the feature is not playing.
   */
  bool MenuHoldsTheScreen() const override { return m_inMenu; }

  /*!
   * \brief Whether the disc is holding one picture until the viewer does something
   *
   * The disc promising that not one more byte is coming. Anything waiting on the disc for
   * bytes has to ask this first, or it waits for as long as the viewer takes -- which, on the
   * thread the splitter reads with, means the graph is never built and nothing is shown.
   *
   * True for a still with no time limit, and for **any** still the disc is holding with
   * buttons on screen: such a still is waiting for the viewer whatever number the disc
   * attached to it, and a timed one allowed to expire simply serves the same menu again for
   * ever.
   *
   * Buttons rather than "a menu is up", and the difference matters. A disc is in its menu
   * domain from well before the menu itself: this one shows two ten-second stills on the way
   * in, and treating those as waiting for the viewer froze it on a picture with nothing to
   * press -- `button 1 -> 1, of 0 on this menu`. Something has to be choosable before waiting
   * for a choice makes sense.
   */
  bool WaitingForTheViewer() const { return m_still && (m_stillIsIndefinite || m_hasButtons); }

  /*!
   * \brief Whether the disc is holding a picture at all, for any reason and any length
   *
   * The weaker question, and the right one only while the splitter is still scanning: no
   * more bytes are coming *just now*, whether or not they will come again by themselves.
   */
  bool HoldingStill() const { return m_still; }

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

  /*! \name The disc's own subtitles
   *
   * A DVD's subtitles are not the splitter's to offer, and leaving them to it is why they never
   * worked. They are subpicture packets inside the very same program stream the splitter is
   * handed, they appear only when there is something to say -- ninety seconds into the feature
   * on the disc this was found on -- and the sixteen colours they are drawn from live in the
   * disc's tables rather than anywhere in the stream. So LAV Splitter scans the opening of a
   * film, finds no subpicture at all, and builds a graph with no subtitle pin: nothing to list,
   * nothing to choose and nothing on screen, however long the viewer waits.
   *
   * The disc's own tables know all of it -- how many streams there are, what language each is,
   * which one it wants and whether it wants them shown -- and the menus already prove the
   * pipeline that draws subpicture over madVR. So the tracks are the disc's, and the pictures
   * are decoded and put on screen here, exactly as a menu is.
   * \{
   */
  int SubtitleCount() const;
  void SubtitleInfo(int index, SubtitleStreamInfo& info) const;
  int CurrentSubtitle() const;
  void ChooseSubtitle(int index);
  bool SubtitlesVisible() const { return m_subtitlesOn; }
  void ShowSubtitles(bool visible);

  //! \brief Whether the disc itself asked for subtitles on what it is playing now
  bool DiscWantsSubtitles() const;

  //! \brief Kodi's own subtitle delay, in seconds, applied where the subtitles are timed
  //! \{
  float SubtitleDelay() const { return m_subtitleDelay.load() / 10000000.0f; }
  void DelaySubtitles(float seconds) { m_subtitleDelay = static_cast<int64_t>(seconds * 1e7); }
  //! \}
  /*! \} */

  /*!
   * \brief Note that the stream the splitter will be given starts about here
   *
   * A subtitle carries the disc's own timestamp, and the splitter numbers the stream it is
   * given from zero -- so the two only line up once the stream's first timestamp is known.
   * The source filter is the only thing that knows which block that is: it reads ahead through
   * whatever the disc passes on the way and throws it away, and where it finally stops throwing
   * away is where the presented stream begins.
   *
   * Called for each block that goes into an empty stream, so a restart moves the origin with it.
   */
  void NoteStreamOrigin();

  /*!
   * \brief The frame the renderer is putting on screen, as the graph timed it
   *
   * A subtitle has to appear with the picture it belongs to, and this is the only honest clock
   * for that. The graph's own position cannot answer it: LAV Splitter clamps that to a duration
   * a navigated disc does not have, and it sits at the end for the whole of a film -- measured
   * at 12.045s of 12.045s while the renderer was two minutes in.
   *
   * Called from the renderer's thread, so it only records the number. What it means is worked
   * out on the thread that draws, see AdvanceSubtitles.
   */
  static void NoteFrameTime(int64_t frameStart);

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
   * \brief Decode one subpicture packet of the feature and queue what it turns into
   *
   * The same decoder a menu goes through, but the answer is treated quite differently. A menu
   * is one picture that stands until the disc replaces it; a subtitle is a picture with a
   * moment to appear and a moment to go, and putting one on screen as though it were a menu is
   * what left a film's subtitles stuck over the picture the first time this was tried.
   */
  void CollectSubtitle(const uint8_t* packet, size_t length, double pts);

  /*!
   * \brief Put on screen whatever subtitle belongs to the frame being drawn, and take off
   *        whatever has had its time
   *
   * Called from the thread that draws rather than from the one that decodes, because it is the
   * drawing that has to be in time.
   */
  void AdvanceSubtitles(int64_t frameStart);

  //! \brief Throw away subtitles decoded for something that is no longer being played
  void ClearSubtitles();

  /*!
   * \brief A moment on the disc's clock, as the renderer will number it
   * \return The renderer's own units, or -1 while there is not yet enough to say
   */
  int64_t Where(double discTime) const;

  //! \brief Note that the stream has reached this timestamp at this many bytes
  void MarkStream(double videoPts);

  //! \brief How far into the stream the splitter has read, on the disc's clock, -1 if unknown
  double WhatTheSplitterIsReading() const;

  /*!
   * \brief Move the selection and say what the disc made of it
   *
   * All four directions do the same thing to a different libdvdnav call, and what is worth
   * knowing is the same for each: whether the disc's own selected button actually moved.
   */
  void MoveSelection(const char* what, void (CDVDInputStreamNavigator::*move)());

  //! \brief Notice when the disc has moved to a different programme
  void NoteProgramme();

  /*!
   * \brief Keep a block back from the graph that is about to be taken down
   *
   * A disc cannot be rewound, so every block taken off it between the viewer choosing
   * something and the new graph opening its stream is a block the new programme will never
   * see again. The stream that is playing is told to stop the instant the change is noticed,
   * but that instant falls inside a read: the very call that reported the change goes on to
   * return the first block of the new programme, and a batch of blocks was already being
   * filled around it. Handed to the outgoing stream, those bytes are thrown away with it.
   *
   * That is not a theoretical loss. Pressing the menu key while an episode was playing cost
   * the menu's opening 128 kB, which is where its one I-frame lives -- the rebuild opened on
   * the seven blocks that were left, LAV Splitter found no video in them and refused to
   * connect at all, and the screen stayed black with no graph and no way back to the menu.
   */
  void Carry(const uint8_t* block, int length);

  /*!
   * \brief Hand back a block kept for this programme, if there is one
   * \return Bytes written to the buffer, 0 when nothing is waiting
   */
  int TakeCarried(uint8_t* buffer, int size);

  //! \brief Whether enough has been kept back that the disc should not be read any further
  bool CarriedEnough() const;

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
  //! Whether the NAV packet the disc is on carries any buttons, kept in step alongside
  //! m_inMenu. "In a menu" starts well before there is anything to press, see
  //! WaitingForTheViewer.
  std::atomic<bool> m_hasButtons{false};
  //! Whether the disc is still being got to something worth showing, see OpeningUp
  std::atomic<bool> m_openingUp{true};

  //! Last programme noticed, so a change is announced once. Noticed from the reading thread
  //! and from the thread carrying a key press, hence atomic.
  std::atomic<int64_t> m_programmeSeen{-1};
  std::atomic<bool> m_announceChanges{false};

  //! Set from the moment a change of programme is noticed until the graph being built for it
  //! opens its stream. While it is set the disc's bytes belong to nobody yet, so they are put
  //! aside rather than given to the stream that is being taken down, see Carry().
  std::atomic<bool> m_handingOver{false};

  //! The bytes put aside, and how much of them has been handed on
  mutable CCriticalSection m_carryLock;
  std::vector<uint8_t> m_carried;
  size_t m_carriedTaken{0};

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

  //! The programme the disc's subtitle tables were last reported for, and how many packets of
  //! each subpicture stream have gone past. Only ever touched from the thread reading the disc.
  int64_t m_surveyed{-1};
  std::map<int, uint64_t> m_subpictureSeen;

  /*!
   * Decodes the feature's subtitles. A decoder of its own rather than the menu's: the two are
   * the same stream but never the same picture, and half of one packet followed by half of the
   * other decodes to nothing at all. Only ever touched from the thread reading the disc.
   */
  CDVDDemuxSPU m_subs;

  //! Whether subtitles are being shown, and which stream. The stream is an index into the
  //! disc's own table; -1 leaves the choice to the disc, which is where it starts.
  std::atomic<bool> m_subtitlesOn{false};
  std::atomic<int> m_subtitleWanted{-1};

  //! Where the stream handed to the splitter begins on the disc's clock, in DVD_TIME_BASE
  //! units, and the first timestamp of the block just read. Reading thread only. The smallest
  //! is kept while the answer settles, see NoteStreamOrigin.
  double m_origin{-1.0};
  double m_originSmallest{-1.0};
  bool m_originWanted{true};
  uint32_t m_originBlocks{0};
  double m_blockPts{-1.0};
  //! The newest video timestamp read off the disc, which is where the stream being handed to
  //! the splitter has got to
  double m_lastVideoPts{-1.0};

  //! Where the stream had got to at a given byte of it, so the splitter's read pointer can be
  //! turned back into a moment. Reading thread only, except for the lookup, which only ever
  //! reads and can be a mark out of date without mattering.
  struct SMark
  {
    long long at;
    double pts;
  };
  std::deque<SMark> m_marks;

  //! Kodi's own subtitle delay, in the renderer's units
  std::atomic<int64_t> m_subtitleDelay{0};
  //! The timestamp that went with the bytes put aside for the next graph, see Carry()
  double m_carriedPts{-1.0};

  //! A subtitle decoded, and when the renderer should have it on and off screen. Both in the
  //! graph's own units, so the drawing thread has nothing to work out.
  struct SPending
  {
    int64_t from;
    int64_t until;
    std::shared_ptr<CDVDOverlaySpu> picture;
  };

  //! Filled by the thread reading the disc, emptied by the thread drawing
  mutable CCriticalSection m_subtitleLock;
  std::deque<SPending> m_pending;
  std::shared_ptr<CDVDOverlaySpu> m_showing;
  int64_t m_showingUntil{0};
  uint64_t m_subtitlesShown{0};
  uint64_t m_subtitlesDecoded{0};
  uint64_t m_subtitlesMissed{0};

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
