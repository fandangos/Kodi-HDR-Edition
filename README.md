<h1 align="center">Kodi — DSPlayer Disc Edition</h1>

<p align="center">
  <strong>Blu-ray, 4K Blu-ray and DVD with their own menus, played through madVR.</strong>
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%20x64-blue?style=flat-square">
  <img alt="Base" src="https://img.shields.io/badge/based%20on-Kodi%2022%20Piers-blue?style=flat-square">
  <a href="https://github.com/fandangos/Kodi-HDR-Edition/releases/tag/dsplayer-v0.4"><img alt="Latest" src="https://img.shields.io/badge/latest-v0.4-brightgreen?style=flat-square"></a>
  <a href="https://forum.kodi.tv/showthread.php?tid=388126"><img alt="Forum" src="https://img.shields.io/badge/support-forum%20thread-orange?style=flat-square"></a>
</p>

<p align="center">
  <a href="https://github.com/fandangos/Kodi-HDR-Edition/releases/tag/dsplayer-v0.4"><strong>⬇ Download the installer</strong></a>
</p>

<!-- Screenshots: Kodi's own UI is safe to show. A disc menu capture is not, unless
     the disc is openly licensed (the Blender open movies have Blu-ray releases).
     ![The player OSD](docs/osd.png)
-->

---

## What it does

Kodi has never played disc menus on Windows. This build does, by playing through
DirectShow with madVR as the renderer:

- **Blu-ray and 4K Blu-ray with menus**, from an ISO or from an unpacked disc folder
- **DVD with menus**, from an ISO or from a `VIDEO_TS` folder
- **madVR** for the picture, so HDR, tone mapping and scaling are madVR's own
- Subtitle files sitting beside a disc are offered alongside the disc's own tracks

It is not finished, but it is comfortable to use daily. Read the limitations below
before you install.

## Before you install

This build drives components it does not ship. Install these first:

| Needed | Notes |
| --- | --- |
| **madVR** | The renderer. Register both `madVR64.ax` and `madVR.ax`. |
| **LAV Filters** | Splitting and decoding. |
| **xy-VSFilter** | Subtitles. Tested against xy-VSFilter pfmod 3.2.0.810. |
| **Java JRE 8** | Runs a Blu-ray's BD-J menus. **Version 8 only** — newer runtimes break libbluray. |

Without madVR and LAV Filters nothing will play. Without a Java 8 runtime, discs
whose menus are BD-J will not reach their menu.

## Using it

| Key | What it does |
| --- | --- |
| **M** | Opens the disc menu. Press again for Kodi's own OSD. |
| **D** | Switches the remote between the disc's menu and Kodi's OSD. |

Settings for madVR and LAV Filters are **not** mirrored into Kodi's interface, and
there is no plan to add them. Alt-Tab to the component's own dialog, change what
you need, and come back.

## Known limitations

Worth knowing before you decide this fits:

- **DVD subtitles are not selectable.** The splitter cannot list them.
- **Multi-angle discs** are not handled properly yet.
- **Exclusive mode must be off.** With `dsplayer.exclusivemode` enabled a disc
  reaches its menu but no overlay is drawn.
- Some discs fail during a rebuild of the graph in madVR itself; this predates
  this build and is not specific to it.
- This is Windows x64 only, and will stay that way.

## What is planned

No promises on any of these, and time is limited:

- Selectable DVD subtitles
- Multi-angle discs
- Following newer Kodi releases

## Fixed here, relative to the DSPlayer build this started from

- Duplicate audio tracks
- Subtitles that never appeared
- Every subtitle file beside a video is now offered, not just one of them
- Kodi's subtitle font, size, style, colour, opacity, border and background now
  reach the screen instead of being ignored

## Building it

This is a Kodi fork; it builds the way Kodi builds on Windows, with the DSPlayer
components added. Four Visual Studio solutions under the tree are built separately
from CMake — see `tools/buildsteps/windows/`. The installer comes from
`tools/buildsteps/windows/x64/BuildSetup.bat`.

## Support

The [forum thread](https://forum.kodi.tv/showthread.php?tid=388126) is the place —
there is no issue tracker here.

## Credits

Kodi is made by [Team Kodi](https://kodi.tv/). DSPlayer itself, and the v21 work
that made this possible, is [tiben20's](https://github.com/tiben20/xbmc). Disc
playback rests on [libbluray](https://www.videolan.org/developers/libbluray.html)
and libdvdnav, and the picture on madVR.

---

<sub>
An unofficial build. Not made, supported or endorsed by Team Kodi — please do not
report problems with it to them. GPLv2, like Kodi itself.
</sub>
