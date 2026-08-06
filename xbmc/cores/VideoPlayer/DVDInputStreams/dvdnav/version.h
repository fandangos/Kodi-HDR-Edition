/*
 * Consumer-side copy of libdvdnav's public headers, with the enumerators the
 * Windows SDK also declares (strmif.h, IDvdControl2's DVD_MENU_ID and DVD_DOMAIN)
 * prefixed with an underscore. DSPlayer includes the DirectShow headers and these in
 * the same translation units and they cannot both use the unprefixed names.
 *
 * The rename is deliberately NOT applied to libdvdnav itself: enumerators are a
 * compile-time matter and the library's ABI is plain integers, so the library builds
 * unpatched with its own names while Kodi consumes these.
 *
 * These are reached because DllDvdNav.h includes them in QUOTES, which resolves
 * relative to the including file before any -I path. Change those to angle brackets
 * and the depends copy silently wins and the build breaks in strmif.h.
 *
 * Regenerate from project/BuildDependencies/x64/include/dvdnav after a libdvdnav bump.
 */

/*
* This file is part of libdvdnav, a DVD navigation library.
*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
*/
#ifndef LIBDVDNAV_VERSION_H
#define LIBDVDNAV_VERSION_H

#define DVDNAV_VERSION_CODE(major, minor, micro) \
     (((major) * 10000) +                         \
      ((minor) *   100) +                         \
      ((micro) *     1))

#define DVDNAV_VERSION_MAJOR 7
#define DVDNAV_VERSION_MINOR 0
#define DVDNAV_VERSION_MICRO 0

#define DVDNAV_VERSION_STRING "7.0.0"

#define DVDNAV_VERSION \
    DVDNAV_VERSION_CODE(DVDNAV_VERSION_MAJOR, DVDNAV_VERSION_MINOR, DVDNAV_VERSION_MICRO)

#endif
