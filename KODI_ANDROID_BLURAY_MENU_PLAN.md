# Kodi Android — Blu-ray / DVD Interactive Menu Playback (Planning)

**Date:** 2026-08-06 · **Goal:** bring interactive Blu-ray (HDMV **and** BD-J) disc-menu
playback to Kodi on Android ARM64, then port the external-subtitle work done on the Windows
DSPlayer side (see `/mnt/nas/HANDOFF-bluray-menus.md`).

This is a **planning document**, not a set of applied changes. It records exactly what makes
Java/BD-J work on Android, what the 2023 build did, what upstream Kodi has since changed, and
the concrete work items — with the one hard blocker found by inspection called out up front.

Prior art (both proven, both mine):
- `github.com/fandangos/Kodi-HDR-Edition` branch `Android-Bluray-Menu-2023` (Kodi 20 Nexus)
- `github.com/fandangos/JRE-Kodi-Android` — the `script.service.jre` addon that ships the JRE

---

## 0. Plan of record & build status — DECIDED / STARTED 2026-08-06

**Distribution decision: this is our own sideloaded Kodi APK** (distinct package name), **not**
a stock-Kodi addon. That single decision dissolves the hardest question: the env vars can be
set in Kodi's C++ (`setenv`), so the whole `os.environ`-doesn't-propagate / `ctypes`-maybe-
missing problem is **moot**. We ship the binary, so we patch the binary. (For the record: a
truly zero-modification addon is *not* achievable — Kodi's Android CPython is built without
`putenv`/`setenv`, confirmed in `pyconfig.h`, so `os.environ` from Python never reaches the C
environment. That was the real 2023 blocker.)

**JRE delivery decision (2026-08-06): NO addon. The APK carries the JRE.** "Whoever wants
Blu-ray disc playback on Android needs this APK" — so the JRE is baked into the APK's own asset
payload and staged by Kodi itself. The `script.service.jre` addon is retired (kept only as a
historical reference for how the image was built).

### Build status — Step 1 DONE & VERIFIED, JRE now APK-native (device test pending)

| item | state | evidence |
|---|---|---|
| libbluray `aarch64→arm` search-path fix | ✅ patch `005-aarch64-java-arch.patch`, rebuilt | `strings libbluray.a` now shows `lib/aarch64/server` (was `lib/arm/server`) |
| `PlatformAndroid.cpp` env block, clean `special://xbmc/j2re-image/` path | ✅ added, relinked | `strings libkodi.so` shows `special://xbmc/j2re-image/` + `_JAVA_OPTIONS` |
| JRE bundled into APK assets (no addon) | ✅ packaging `Makefile.in` + staged tree | `sharedapk` copies `$(JRE_IMAGE_DIR)` → `xbmc/assets/j2re-image` after the `.so` strip |
| BD-J jars renamed 1.3.2→**1.5.0** inside the image | ✅ | libbluray demands `libbluray-j2se-1.5.0.jar`; AWT name derived (`bdj.c:627`) |
| Kodi APK with JRE inside | ✅ built & verified | `~/kodi/kodiapp-arm64-v8a-debug.apk` (package `org.xbmc.kodi`); zip OK, packaged `libkodi.so` carries the env strings, 159/159 JRE files incl. `libjvm.so`+`rt.jar` in `assets/j2re-image` |

The `LIBBLURAY_CACHE_ROOT`/`PERSISTENT_ROOT` env vars from 2023 were **deliberately dropped** —
upstream now sets them in code (`DVDInputStreamBluray.cpp:1287-1291`).

### Why `special://xbmc/j2re-image` (the path, done right this time)

The 2023 `special://xbmcbin/../../../cache/lib/` was a hack **and** landed in the OS-evictable
`cache/` dir. Android storage forces the choice:

- **External** (`special://home`, `special://temp` on Android = `/sdcard/Android/data/...`) is
  mounted **`noexec`** → cannot `dlopen` `libjvm.so`. Disqualified.
- **APK interior** (assets/`lib`) is a sealed zip — not real files; a JVM can't run from it.
- **Internal app data** (`/data/user/0/<pkg>/…`) is the only writable **and** executable home.

`special://xbmc` = `KODI_HOME` = `<files>/apk/assets` (internal, **persistent**, and Splash
re-extracts it on every app update so it always matches the APK's libbluray). We ship the JRE
as `assets/j2re-image`; Splash unpacks it there for free. No `../`, no eviction, no addon.

**Packaging gotcha handled:** `Makefile.in:46` deletes every `*.so` from `assets/` (Kodi routes
its own libs via `jniLibs`). The JRE copy is therefore injected in `sharedapk` **after** that
strip, straight into `xbmc/assets/j2re-image`, so `libjvm.so` survives.

### What YOU must do on-device to complete Steps 1 & 2

1. **Set `disc.playback = 2`** — Settings ▸ Player ▸ Discs ▸ *Blu-ray playback mode* = **Show
   disc menu**. Without it Kodi's playlist chooser intercepts the disc and no menu appears.
2. **Step 1 (HDMV, no Java):** install the APK, set (1), play an HDMV disc/ISO. Menus should
   draw with **no JVM at all** — validates the disc-menu path independent of the JRE.
3. **Step 2 (BD-J):** just play a BD-J/UHD disc (JRE is already in the APK — no separate
   install). First launch after update re-stages assets, so give it a moment. Watch `adb
   logcat`: `Using JAVA_HOME` → `libjvm.so` dlopen OK = win; `libbluray-j2se-1.5.0.jar not
   found` = jar issue; linker/`execmem` denial = the Android sandbox risk (§5).

### One decision still open

- **Package name.** Current APK is `org.xbmc.kodi` — it will *replace* official Kodi. For the
  "sideload alongside" goal, set `APP_PACKAGE org.xbmc.kodi.fandangos` in `version.txt` and
  rebuild (as the 2023 branch did). Our env path is package-relative, so the rename is safe.
  Skip only if the test device has no official Kodi. **Say the word and I'll flip + rebuild.**

### Corrections to earlier notes (accuracy over consistency)

- **The whole JRE cannot live in `jniLibs`.** A JRE is a directory tree
  (`lib/aarch64/server/libjvm.so`, `lib/rt.jar`, …) and Android's native-lib dir is *flat*, so
  the "put it in jniLibs for SELinux safety" idea does **not** apply to the JRE as-is. It now
  lives in **APK assets**, extracted by Splash to internal storage. The jniLibs idea survives
  only as a *mitigation* if a strict device blocks `dlopen` from that extracted (writable)
  storage: ship just the `.so` files as jniLibs, symlink them into the tree, and bridge with
  `-Dsun.boot.library.path=` / `-Djava.home=` via `_JAVA_OPTIONS`. Contingency, not default.
- **Jar reconciliation is real, not theoretical.** `libkodi.so` prints
  `searching for libbluray-j2se-1.5.0.jar`, and libbluray *derives* the AWT jar name by
  inserting `awt-` (`bdj.c:627`) — so **both** jars were renamed to 1.5.0. If BD-J discs
  misbehave, the fallback is rebuilding the jars from 1.5.0 source (ant + JDK; the
  `ENABLE_BLURAY_JAR` path in `FindBluray.cmake:32-74`).
- **`xmlparser.jar` is absent** from the image; libbluray appends
  `$JAVA_HOME/lib/xmlparser.jar` to bootclasspath (`bdj.c:925`) but as a non-fatal `/a:` append
  (2023 ran without it). Add it later only if an XML-heavy BD-J title needs it.

---

## 1. How BD-J actually runs on Android (the mechanism)

Kodi's Blu-ray path is `CDVDInputStreamBluray` → **libbluray**. libbluray decodes HDMV menus
in C, but **BD-J menus require a real JVM**: libbluray `dlopen`s `libjvm.so` at runtime and
runs the disc's Java Xlets against a small BD-J class library. Android ships no
native-accessible JVM (ART is not usable via the JNI *Invocation* API the way HotSpot is), so
**three things must be supplied at runtime**:

1. **A JVM** — `libjvm.so` (HotSpot) built for **aarch64 + bionic**, i.e. an actual
   Android-compatible OpenJDK, not a glibc desktop JRE.
2. **The BD-J class library jars** — `libbluray-j2se-<ver>.jar` + `libbluray-awt-j2se-<ver>.jar`
   (the `org.videolan` / AWT bridge the disc's Xlets link against).
3. **Environment pointers** so libbluray finds (1) and (2):
   - `JAVA_HOME` / `JDK_HOME` → the JRE image root (libbluray searches it for `libjvm.so`)
   - `LIBBLURAY_CP` → the directory holding the BD-J jars
   - `_JAVA_OPTIONS=-Djava.io.tmpdir=<writable dir>` (HotSpot needs a writable temp)

libbluray's cache/persistent roots (`BLURAY_PLAYER_CACHE_ROOT` / `PERSISTENT_ROOT`) are the
fourth requirement, but **upstream now sets these in code** — see §3.

### The JRE image (reuse the 2023 artifact — do NOT rebuild lightly)

The existing `script.service.jre-aarch64-v1.2.zip` (GitHub Releases, 44 MB) contains
`resources/lib/j2re-image.zip`, which unpacks to an **OpenJDK 8 (1.8.0) aarch64 JRE** already
compiled for Android/bionic:

```
j2re-image/bin/java
j2re-image/lib/aarch64/server/libjvm.so     <- HotSpot, the actual VM
j2re-image/lib/aarch64/libjava.so
j2re-image/lib/rt.jar, jce.jar, jsse.jar ...  (standard JDK8 class library)
j2re-image/libbluray-j2se-1.3.2.jar          <- BD-J runtime  }  the jars
j2re-image/libbluray-awt-j2se-1.3.2.jar      <- AWT bridge    }  LIBBLURAY_CP points here
j2re-image/libbluray.jar                      (bootstrap)
```

Building a bionic OpenJDK from scratch (porting OpenJDK to the NDK) is a large project on its
own; **the plan reuses this proven image.** JDK 8 is also the correct target — BD-J is a
JavaME/JavaSE-8-era spec.

---

## 2. THE ONE HARD BLOCKER — libbluray maps `aarch64 → "arm"`

Found by disassembling the depends-built lib, not assumed. Kodi's current depends build
(`tools/depends/target/libbluray`, libbluray **1.5.0**) already produced
`xbmc-depends/.../lib/libbluray.a`, and its compiled JVM search paths are:

```
lib/server         jre/lib/arm/server     lib/arm/server
lib/client         jre/lib/arm/client     lib/arm/client
```

There is **no `lib/aarch64/server`** in that list — but the JRE ships `libjvm.so` exactly
there. Root cause is upstream libbluray `meson.build:83-90`:

```meson
elif host_machine.cpu_family() == 'aarch64'
    java_arch = 'arm'          # <-- wrong for a standard OpenJDK aarch64 layout
```

`JAVA_ARCH` is baked into the search strings at compile time, so on aarch64 libbluray looks
under `lib/arm/…` and **never finds the aarch64 `libjvm.so`**. This is precisely what the
"modified libbluray 1.3.2" fixed in 2023. **Two ways to resolve it now — pick one:**

| option | change | verdict |
|---|---|---|
| **A. Patch libbluray** | add `005-android-java-arch.patch` in `tools/depends/target/libbluray/` flipping `java_arch = 'aarch64'` (guard on Android so other targets are untouched) | clean, mirrors 2023, keeps JRE layout as-is |
| **B. Repackage JRE** | move `libjvm.so` (and `libjava.so`) to `$JAVA_HOME/lib/server/` (arch-less) | **no source patch** — stock lib already searches `lib/server`; but re-lays out the released image |

**Recommended: Option A** — one guarded patch in the depends tree, no touching the binary
JRE, and it keeps the image byte-identical to the 2023 releases. Note the current samba
Makefile edit already in the tree shows depends patches are the established pattern here.

---

## 3. What upstream Kodi already gives us (big wins since 2023)

The 2023 branch had to add BD-J plumbing by hand. Current master already has most of it:

- **`HAVE_LIBBLURAY_BDJ` is compiled on Android by default.** `cmake/modules/FindBluray.cmake:224-226`
  defines it for every platform except windowsstore, so all the BD-J overlay/callback code in
  `CDVDInputStreamBluray` is already in the Android build.
- **Cache/persistent roots are set in code now.** `DVDInputStreamBluray.cpp:1287-1291` calls
  `bd_set_player_setting_str(BLURAY_PLAYER_PERSISTENT_ROOT / CACHE_ROOT, special://userdata/cache/bluray/…)`.
  → The 2023 `LIBBLURAY_CACHE_ROOT` / `LIBBLURAY_PERSISTENT_ROOT` env vars are now **redundant**
  and should NOT be re-added.
- **Disc-menu + main-playlist work landed upstream** (the same commits the Windows handoff §-18
  inherits): disc menu caching, main-playlist selection, and a real `disc.playback = 2`
  ("Show disc menu") path that verifies BD-J. The user's own insight holds: **once a JVM is
  present, Kodi's internal VideoPlayer already knows how to drive the menu.**
- **`FindBlurayBDJ.cmake`** exists to locate/stage BD-J jars — relevant if we ever build the
  jars in-tree instead of shipping them in the addon.

So the residual C++ change vs. 2023 is small: **just the env vars in `PlatformAndroid.cpp`.**

---

## 4. Concrete work items (in order)

1. **Fix the arch mismatch (§2, Option A).** Add a guarded depends patch so aarch64 libbluray
   searches `lib/aarch64/server`. Rebuild libbluray in depends; verify with
   `strings …/lib/libbluray.a | grep aarch64/server`.

2. **Re-add the runtime env in `xbmc/platform/android/PlatformAndroid.cpp` `InitStageOne()`**
   (currently lines ~34-46, right after the `OS`/`SSL_CERT_FILE` setenvs). Port from 2023 but
   **drop the two now-redundant cache/persistent vars**:
   ```cpp
   const std::string jre =
       CSpecialProtocol::TranslatePath("special://xbmcbin/../../../cache/lib/j2re-image/");
   setenv("JAVA_HOME",   jre.c_str(), 1);
   setenv("JDK_HOME",    jre.c_str(), 1);
   setenv("LIBBLURAY_CP",jre.c_str(), 1);           // BD-J jars live in the image root
   setenv("_JAVA_OPTIONS", ("-Djava.io.tmpdir=" + jre).c_str(), 1);
   ```
   `special://xbmcbin/../../../cache/lib/` resolves to the app's writable
   `/data/data/<pkg>/cache/lib/` — where the addon extracts the JRE (§5). This path (not
   `special://xbmcbin/j2re-image`) is what made the 2023 install **persistent** across app
   restarts (commit `e161bed7`).

3. **Ship the JRE via the `script.service.jre` addon (reuse as-is for now).** On first run
   (`xbmc.service`) `main.py` copies `resources/lib/j2re-image.zip` into `cache/lib/` and
   unzips it; on later runs it detects the jar and skips. Reuse the aarch64 v1.2 release.
   - Later refinement: bundle the JRE straight into the APK (`assets/` or a `lib/` payload)
     so no separate addon step is needed. Deferred — the addon works and is low-risk.

4. **BD-J jar version.** The image ships **1.3.2** jars while the lib is **1.5.0**. libbluray's
   jar name is version-stamped (`BDJ_JARFILE = "libbluray-j2se-" VERSION ".jar"`, bdj.c:55-57),
   and it looks for the matching version via `LIBBLURAY_CP`. **Verify** whether 1.5.0 insists on
   `libbluray-j2se-1.5.0.jar`; if so, either (a) rebuild the jars at 1.5.0 (needs `ant` + JDK on
   the build host — see `ENABLE_BLURAY_JAR` path in `FindBluray.cmake:32-74`) and drop them into
   the image, or (b) symlink/rename the 1.3.2 jars to 1.5.0 and test (BD-J API is largely
   stable across these). Resolve this before declaring BD-J done.

5. **`disc.playback = 2` (Show disc menu).** Same finding as the Windows side (handoff §-18):
   at `0`/`1` Kodi's playlist chooser intercepts the disc before the menu path runs. Must be set
   for menus to appear. Decide: ship it as a default via the addon (2023 shipped a
   `decoderfilter.xml` + `keyboard.xml` the same way) or document it.

6. **Background-video menus / decoderfilter (optional, UX).** 2023 toggled two code paths
   (`DVDVideoCodecAndroidMediaCodec.cpp` codec validity, `VideoPlayer.cpp` `DVDSTATE_STILL`)
   then **reverted them** in favour of shipping a `decoderfilter.xml` profile (commit
   `462269633`). Prefer the profile — no core patch. Keep the optional `keyboard.xml` keymap for
   remotes with few keys.

---

## 5. Runtime risks to watch (Android-specific, verify on real devices)

- **W^X / SELinux `execmem`.** HotSpot "server" JITs code and needs executable memory;
  `dlopen` of `libjvm.so` from an app-writable dir can be blocked on newer Android
  (apps targeting API 29+). It worked in 2023 on the tested devices; the new APK targets a much
  higher API (`android-37`, per `KODI_ANDROID_BUILD_HANDBOOK.md`). If BD-J init fails with a
  linker/`execmem` denial, fallbacks: force interpreter (`-Xint` via `_JAVA_OPTIONS`), or load
  the JVM from the APK's approved native-lib dir instead of `cache/lib/`.
- **`extractNativeLibs` / dlopen path policy** — same class of issue; the APK may need
  `android:extractNativeLibs="true"` if the JVM is moved into the native lib dir.
- **HDMV menus are the low-risk win.** They need **no JVM at all** — libbluray decodes them in
  C. If BD-J proves hard on modern Android, HDMV disc menus (most DVDs and many Blu-rays) should
  work purely from §2 + §4.5, independent of the JRE. Consider validating HDMV first.

---

## 6. After menus — port the DSPlayer external-subtitle work

The Windows DSPlayer effort (`/mnt/nas/HANDOFF-bluray-menus.md`) added: menus over madVR,
external subtitles beside BDMV/ISO/DVD, DVD subpicture decoding the splitter can't list, and a
subtitle-hold rule (held over a disc's own menu, released for the feature, never held over a
popup). **On Android this is much smaller** because Kodi's internal VideoPlayer — not a
DirectShow graph — plays the feature once the JVM is present:

- **External subtitles for BDMV/ISO** — Kodi's internal subtitle path already loads sidecar
  subs for ordinary files; the work is making the same discovery run for a disc source. No
  madVR, no XySubFilter, no DirectShow graph rebuild.
- **DVD subpicture / menu subtitles** — internal VideoPlayer already decodes DVD subs; the
  hard part on Windows (LAV building a graph with no subtitle pin) does not exist here.
- **`M` toggles menu ↔ Kodi OSD** — the user's requested UX: press `M` → disc popup menu,
  press again → Kodi OSD. Small keymap/action change, likely on top of `ACTION_SHOW_VIDEOMENU`
  (note the Windows handoff's caution: the action is `showvideomenu`, not `menu`).

Port this **after** BD-J menus are confirmed on a device — sequence it, don't parallelise.

---

## 7. Verification plan

1. `strings …/lib/libbluray.a | grep aarch64/server` → confirms §2 patch took.
2. Build APK per `KODI_ANDROID_BUILD_HANDBOOK.md`; install; install the JRE addon; restart Kodi.
3. `adb logcat | grep -iE 'BDJ|libbluray|JAVA_HOME|libjvm|BD-J'` while opening a disc:
   - "Using JAVA_HOME '…'" then a successful `libjvm.so` dlopen = JVM found (§2 fixed).
   - `libbluray-j2se-….jar not found` = §4.4 jar-version issue.
   - linker/`execmem`/permission denial = §5 sandbox issue.
4. Open an **HDMV** disc first (no JVM needed), then a **BD-J** disc, then the `M`-key popup.
5. Confirm `special://userdata/cache/bluray/{cache,persistent}` get populated (proves the
   in-code roots from §3 are writable).

---

## Open questions to settle before coding

- **§2**: patch libbluray (A) vs. repackage the JRE (B) — recommend A.
- **§4.4**: do the 1.3.2 BD-J jars satisfy libbluray 1.5.0, or must they be rebuilt at 1.5.0?
- **§5**: does the target device/Android version allow `dlopen` of a JIT VM from app storage?
- **§4.3**: keep the separate JRE addon, or bundle the JRE into the APK?
