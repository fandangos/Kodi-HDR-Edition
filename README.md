<h1 align="center">Kodi — Disc Menu Editions</h1>

<p align="center">
  <strong>Kodi builds that play Blu-ray, 4K Blu-ray and DVD discs with their real menus.</strong>
</p>

<p align="center">
  <img alt="Downloads" src="https://img.shields.io/github/downloads/fandangos/Kodi-HDR-Edition/total?label=downloads&style=flat-square">
  <img alt="License" src="https://img.shields.io/badge/license-GPLv2-blue?style=flat-square">
  <img alt="Base" src="https://img.shields.io/badge/based%20on-Kodi%2022%20Piers-blue?style=flat-square">
</p>

---

## Pick your build

<table>
<tr>
<td width="50%" valign="top">

### 📀 Blu-ray Menu Edition
**Android · Shield · Android TV**

BD-J menus on Blu-ray and 4K Blu-ray, Dolby Vision from disc, PGS and overlay
tone mapping.

**[⬇ Download APK](https://github.com/fandangos/Kodi-HDR-Edition/releases/tag/android-bluray-menu-2026-v4)**

[Documentation](https://github.com/fandangos/Kodi-HDR-Edition/tree/android-bluray-disc-menus) ·
[Support thread](https://forum.kodi.tv/showthread.php?tid=360250)


</td>
<td width="50%" valign="top">

### 🎬 DSPlayer Disc Edition
**Windows**

Disc menus through madVR — Blu-ray, 4K Blu-ray and DVD, from ISO or from a
folder.

**[⬇ Download installer](https://github.com/fandangos/Kodi-HDR-Edition/releases/tag/dsplayer-v0.4)**

[Documentation](https://github.com/fandangos/Kodi-HDR-Edition/tree/dsplayer_kodi22) ·
[Support thread](https://forum.kodi.tv/showthread.php?tid=388126)


</td>
</tr>
</table>

> The two builds are separate and share no binaries. Android needs no extra
> software; the Windows build needs madVR, LAV Filters, xy-VSFilter and a Java 8
> runtime installed first — see its documentation before downloading.

---

## What this is

Kodi does not play disc menus. These two forks add that: the Android build runs a
disc's BD-J menus on a bundled Java runtime, and the Windows build plays discs
through DirectShow and madVR with libbluray and libdvdnav driving the menus.

Everything else is stock Kodi 22 (Piers). If a thing is not about discs, it
behaves exactly as upstream Kodi does.

## Getting help

Questions, bug reports and requests go to the forum thread for your build —
linked above. There is no issue tracker here.

## Built on the work of others

Kodi is made by [Team Kodi](https://kodi.tv/). The Windows build starts from
[tiben20's DSPlayer](https://github.com/tiben20/xbmc). The Android build would not
exist without Petri Hintukainen, who wrote libbluray and built the Java images it
runs on, and owes a great deal to Shaya Potter and to the
[PojavLauncher](https://github.com/PojavLauncherTeam) developers.

Java runtimes used by the Android build:
[aarch32](https://github.com/fandangos/openjdk-aarch32-jdk8u) ·
[aarch64](https://github.com/fandangos/openjdk-multiarch-jdk8u)

---

<sub>
These are unofficial builds. They are not made, supported or endorsed by Team Kodi,
and problems with them should not be reported to Team Kodi. Licensed GPLv2, like
Kodi itself.
</sub>
