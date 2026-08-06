/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

//
// What the player needs of a disc that runs its own menus, whichever kind of disc it is.
//
// CDSBlurayNavigator and CDSDvdNavigator have nothing else in common -- one drives libbluray
// and the other libdvdnav, and how each answers "is a menu on screen" is completely
// different -- but the player's side is identical: route the viewer's key presses at
// whichever disc is playing, and know whether a menu is up so it can decide between the disc
// and Kodi's own OSD. Expressed once here so that CDSPlayer::OnAction is written against a
// disc rather than against Blu-ray.
//
// Anything format specific stays on the concrete class. The Blu-ray overlay pipeline and the
// playlist handback both reach for CDSBlurayNavigator directly and should keep doing so.
//

#pragma once

#if HAS_DS_PLAYER

/*!
 * \brief A disc presenting its own menus
 *
 * Only one disc plays at a time, and Playing() is it.
 */
class IDSDiscNavigator
{
public:
  virtual ~IDSDiscNavigator() = default;

  //! \brief The disc currently playing, or nullptr
  static IDSDiscNavigator* Playing() { return m_playing; }

  /*!
   * \brief Whether the disc currently has a menu on screen
   *
   * The honest answer to "is the viewer looking at a menu", and so the one that decides where
   * their key presses go. How it is arrived at differs completely between the two formats.
   */
  virtual bool MenuOnScreen() const = 0;

  /*!
   * \brief Whether the menu on screen is the thing being watched
   *
   * A narrower question than MenuOnScreen, and a different one: a top menu, a start menu or a
   * language menu *is* what the viewer is looking at, while a popup menu is drawn over a film
   * that goes on playing underneath. Nothing belonging to the film -- a subtitle file beside
   * the disc above all -- has any business on screen during the first, and no reason to be
   * taken away during the second.
   */
  virtual bool MenuHoldsTheScreen() const = 0;

  /*!
   * \brief Whether the disc itself says its menu is visible
   *
   * A second opinion, worth logging beside the first: the two disagreeing is exactly the
   * state that strands a viewer with no way back to the OSD.
   */
  virtual bool DiscSaysMenuVisible() const = 0;

  //! \brief Ask the disc for its menu, false when it has none to give
  virtual bool ShowMenu() = 0;

  /*! \name The viewer's key presses, in player terms rather than disc ones
   *  \{ */
  virtual void OnBack() = 0;
  virtual void OnUp() = 0;
  virtual void OnDown() = 0;
  virtual void OnLeft() = 0;
  virtual void OnRight() = 0;
  virtual void OnSelect() = 0;
  /*! \} */

  //! \brief Stop reporting changes while the player is opening the disc again
  virtual void SuspendProgrammeChanges() = 0;

  //! \brief Whether the disc has played everything it intends to
  virtual bool Finished() const = 0;

protected:
  //! \brief Called by whichever navigator has just opened or closed a disc
  static void NowPlaying(IDSDiscNavigator* navigator) { m_playing = navigator; }

private:
  static IDSDiscNavigator* m_playing;
};

#endif
