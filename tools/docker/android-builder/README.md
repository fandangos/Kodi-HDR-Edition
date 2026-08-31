# Kodi Android build container

Cross-builds Kodi for Android on any x86_64 Docker host — including Unraid — so a build no
longer depends on one workstation. Two tags, one Dockerfile:

| tag | ABI | `--host` |
|---|---|---|
| `arm64-v8a` | `arm64-v8a` (64-bit) | `aarch64-linux-android` |
| `armeabi-v7a` | `armeabi-v7a` (32-bit) | `arm-linux-androideabi` |

They differ only in the default `KODI_ARCH`; every heavy layer (SDK, NDK, JDK, JREs) is shared,
so pulling both costs little more than pulling one. Either tag can build either ABI if you
override `KODI_ARCH`.

The container runs one build and exits. On Unraid it will show as a stopped container when it
finishes — that is success, not a crash. Read the log.

## Two variants: slim (published) and full (offline)

The Android SDK and NDK are 2.9 GB — one ~1 GB compressed layer, which is more than a domestic
uplink wants to push and more than Docker Hub's upload session will tolerate in one go. So there
are two builds of the same image:

| | `:arm64-v8a`, `:armeabi-v7a` (**slim**, what gets published) | `:<abi>-full` |
|---|---|---|
| SDK + NDK | installed into `/state/android-sdk` on first run | baked into the image |
| ship size | ~0.5 GB | ~1.5 GB |
| first run | +5–10 min, needs `dl.google.com` | nothing extra |
| later runs | no network needed for the SDK | same |
| built by | `--build-arg BAKE_ANDROID_SDK=0` (default in `publish.sh`) | `--build-arg BAKE_ANDROID_SDK=1` (default in the Dockerfile) |

**This does not cost reproducibility.** The slim image installs the *same pinned revisions* —
`ndk;28.2.13676358`, `build-tools;37.0.0` and `;36.0.0`, `platforms;android-37.0` — through
`sdkmanager`, which verifies Google's published checksums. The build inputs are identical; only
the moment of download differs. What it does cost is self-containment: the slim image needs
Google reachable once per state volume, and depends on those revisions remaining in the
repository.

`$ANDROID_HOME` is `/opt/android-sdk` in **both** variants — in the slim one it is a symlink into
`/state`, created before the build drops privileges. That is deliberate, not decoration:
`tools/depends/configure` writes absolute NDK paths into `config.site`, `Toolchain.cmake` and
`cross-file.meson`, so an SDK whose path moved between runs would quietly invalidate a configured
depends tree.

> [!NOTE]
> Switching an existing `/state` from one variant to the other forces one full recompile of Kodi.
> Nothing is broken — the re-downloaded NDK headers simply carry new timestamps, so every object
> that includes them is out of date. Subsequent builds are incremental again.

## What is baked in

- Ubuntu 24.04, JDK 17 (Gradle 9.4.1 rejects Java 21 class files)
- Android SDK cmdline-tools, platform-tools, `platforms;android-37.0`, `build-tools;37.0.0`
  **and `build-tools;36.0.0`** — the second one is not optional. AGP 9.2.0 pins its own
  build-tools revision regardless of Kodi's `compileSdk`, and when it is missing AGP tries to
  download it into the SDK mid-build and dies with *"The SDK directory is not writable"*, because
  the build runs unprivileged. Bump it when `tools/android/packaging/build.gradle` changes AGP.
- **NDK r28c** (`28.2.13676358`) — the revision Kodi CI builds and releases with
- BD-J JRE images for **both** ABIs, at `/opt/kodi-jre/<abi>/j2re-image`
- The libbluray **1.5.0 BD-J jars**, built in-image from the same tarball `tools/depends`
  compiles, with JDK 8 at source/target 1.4 (the combination libbluray's own meson logic picks)
- That **JDK 8 itself**, at `/opt/jdk8` (`BDJ_JDK8_HOME`), so the runtime build can recompile
  the BD-J classes our libbluray patches touch — see below

## What you supply

### One mount or three

`KODI_DATA=/data` derives all three paths from a single directory — `/data/src`,
`/data/state`, `/data/release` — which is the shape you want when each container gets one
directory of its own (an Unraid `appdata` folder, say). Put the keystore in `/data/keys/` and
the whole container is one `-v`. Any of the three can still be overridden individually;
`KODI_SRC` / `KODI_STATE` / `KODI_OUTPUT` win over `KODI_DATA`.

Without `KODI_DATA` the three default to `/src`, `/state` and `/output`:

| mount | purpose |
|---|---|
| `/src` | the Kodi checkout. **Writable** — `tools/depends` builds in-tree. Empty on first run = cloned from `KODI_GIT_URL`. |
| `/state` | Android SDK, depends prefix, tarballs, Gradle home, ccache. Keep it between runs or every build is a cold ~1 h build, and keep it on a **cache-backed** share — it is tens of thousands of small files. |
| `/output` | finished APKs. |

The Unraid templates use the single-mount form: one data directory per container, on a
cache-only share.

Each build writes `kodiapp-<abi>-<config>-<git-rev>-<timestamp>.apk` plus a stable
`kodiapp-<abi>-<config>.apk` symlink pointing at the newest, so old builds are kept and there is
always one predictable filename to grab.

Plus the signing key — mounted as a file, with its password in a `.pass` file beside it, or as
environment variables (see below).

## Quick start

One directory, one mount. Put the keystore in `<dir>/keys/` first; everything else is created
for you, and an empty `src/` is cloned from `KODI_GIT_URL`:

```bash
docker run --rm \
  -v /mnt/user/appdata/kodi-android-builder64:/data \
  -e KODI_DATA=/data \
  -e KODI_ARCH=arm64-v8a \
  -e KODI_APP_PACKAGE=org.xbmc.fandangos \
  -e KODI_BUILD_CONFIG=Release \
  -e KODI_GIT_URL=https://github.com/fandangos/Kodi-HDR-Edition.git \
  -e KODI_GIT_REF=android-bluray-disc-menus \
  -e KODI_GIT_PULL=1 \
  -e KODI_ANDROID_STORE_FILE=/data/keys/fandangos-release.keystore \
  -e KODI_ANDROID_KEY_ALIAS=fandangos \
  <dockerhub-user>/kodi-android-builder:arm64-v8a
```

Budget roughly **22 GB per ABI**: ~14 GB for the source tree once depends has built inside it,
~8 GB of state (2.7 GB SDK, 3.7 GB depends prefix, Gradle home, tarballs).

## Building a new version

With `KODI_GIT_PULL=1` (the template default) the loop is:

1. commit and push to the branch named in `KODI_GIT_REF`
2. press **Start** on the container in Unraid

It fetches, fast-forwards the checkout, rebuilds and drops a new APK in the output share. There is
no terminal step. A rebuild after a few commits is **incremental** — typically 5–15 minutes rather
than the ~1 h cold build — because `/state` and the build directory persist.

### BD-J patches reach the APK without an image rebuild

The jars above are built from a **pristine** tarball while the image is built, long before your
source tree exists — so a patch under `tools/depends/target/libbluray/` that touches
`bdj/java/**` is not in them. Left alone, adding such a patch and pressing Start yields an APK
that silently does not contain it. (Exactly what happened with
`008-all-bdj-await-image-decode.patch`: the fix was in the source, the menu stayed black.)

So the build recompiles the affected classes at run time, from the libbluray source it just
patched and built, and splices them into a per-build copy of the JRE image
(`tools/android/packaging/jre/rebuild-bdj-jars.sh`). Nothing to do by hand: **add the patch,
list it in `cmake/modules/FindBluray.cmake`, push, press Start.** The verification step then
compares the packaged jars' class CRCs against the image's pristine ones and fails the build if
they match, so an unpatched APK cannot slip through.

Two things this does *not* survive:

- **the JDK 8 layer** — an image built before `BDJ_JDK8_HOME` existed has no `/opt/jdk8`, and the
  build stops with *"BDJ_JDK8_HOME is not a JDK 8"*. Rebuild and redeploy the image once:

  ```bash
  cd tools/docker/android-builder
  for arch in arm64-v8a armeabi-v7a; do
    docker build --build-arg "KODI_ARCH=${arch}" --build-arg BAKE_ANDROID_SDK=0 \
                 -t fandangos/kodi-android-builder:${arch} .
  done
  ./deploy-to-unraid.sh <unraid-host> --registry
  ```

  Note this is the LAN route, not Docker Hub — the Unraid templates pull from
  `localhost:5000/kodi-android-builder:<abi>`, a registry on the Unraid box that
  `deploy-to-unraid.sh --registry` pushes into over ssh. (`publish.sh` builds the same slim
  images but pushes them to a Hub namespace, which this setup does not use; the `<Registry>`
  line in the templates is only the project link Unraid shows in the UI.) The tag does not
  change, so **force an update on the container in the Docker tab** afterwards or it keeps
  running the cached image.
- **a bumped `LIBBLURAY_VERSION`** — the jar names carry the version and are pinned in the
  Dockerfile; bump both it and `LIBBLURAY-VERSION` together.

Guard rails, because a silent wrong build is worse than a failed one:

- if tracked files in `/src` have local modifications the build **stops** rather than discarding
  them; set `KODI_GIT_RESET=1` if you want the checkout to always match the remote
- if the local branch has diverged from the remote it **stops** instead of merging
- untracked files are never deleted — no `git clean`. The depends packages and the build directory
  live inside the source tree, and removing them would turn every build back into a cold one
- the remote must be reachable from inside the container: an `https://` URL, not `git@…` (no ssh
  keys) and not a host path

Rebuild triggers worth knowing:

- **changing a patch under `tools/depends/target/<pkg>/`** does not rebuild that package on its
  own — see the note below about the depends prefix
- **changing the ABI, API level or depends build type** needs `KODI_RECONFIGURE=1` once
- `KODI_SKIP_DEPENDS=1` skips straight to the Kodi build; useful when you know only Kodi's own
  sources changed

## Environment variables

| variable | default | meaning |
|---|---|---|
| `KODI_DATA` | – | single directory holding `src/`, `state/` and `release/` |
| `KODI_SRC` / `KODI_STATE` / `KODI_OUTPUT` | `/src` `/state` `/output` | individual paths; override `KODI_DATA` |
| `KODI_ARCH` | tag default | `arm64-v8a` or `armeabi-v7a` |
| `KODI_APP_PACKAGE` | `org.xbmc.fandangos` | Android package id. Asserted against `CMakeCache.txt` and against the finished APK. |
| `KODI_BUILD_CONFIG` | `Release` | `Release`, `Debug` or `RelWithDebInfo` |
| `KODI_NDK_API` | `24` | Android API level for depends |
| `KODI_DEPENDS_DEBUG` | `yes` | depends build type. `yes` reproduces the known-good prefix `<host>-24-debug`. |
| `KODI_JOBS` | all cores | `make -j` |
| `KODI_BUNDLE_BDJ` | `1` | bundle the JRE + BD-J jars. `0` = no disc menus. |
| `KODI_ENABLE_CCACHE` | `0` | ccache for depends |
| `KODI_RECONFIGURE` | `0` | force `./configure` to re-run |
| `KODI_SKIP_DEPENDS` | `0` | skip straight to the Kodi build |
| `KODI_EXTRA_CMAKE_ARGS` | – | appended to `CMAKE_EXTRA_ARGUMENTS` |
| `KODI_GIT_URL` | – | cloned into an empty `/src`; ignored once a checkout exists |
| `KODI_GIT_REF` | – | branch or tag to clone, and the branch the update below follows |
| `KODI_GIT_PULL` | `0` | `1` fetches and fast-forwards `/src` before building |
| `KODI_GIT_REMOTE` | `origin` | remote to fetch from |
| `KODI_GIT_RESET` | `0` | `1` hard-resets to the remote branch instead of refusing to build over local edits |
| `KODI_ALLOW_DEBUG_KEY` | `0` | permit a Release build signed with a throwaway debug key |
| `PUID` / `PGID` | `99` / `100` | Unraid's `nobody:users`; the build drops to this uid |

### Signing

| variable | meaning |
|---|---|
| `KODI_ANDROID_KEYSTORE_B64` | the keystore file, base64. Decoded to a 0600 file in the container's own `/tmp`, never onto the persistent volume, so it dies with the container. |
| `KODI_ANDROID_STORE_FILE` | path to a mounted keystore. Ignored if the base64 variable is set. |
| `KODI_ANDROID_KEY_ALIAS` | key alias |
| `KODI_ANDROID_STORE_PASSWORD` | store password |
| `KODI_ANDROID_KEY_PASSWORD` | key password — defaults to the store password |

Produce the blob with:

```bash
base64 -w0 ~/.android/fandangos-release.keystore
```

The alias and store password are checked with `keytool` before anything compiles, so a wrong
password fails in seconds rather than after the build.

> [!WARNING]
> **An environment variable is not a secret store.** `KODI_ANDROID_KEYSTORE_B64` is visible to
> `docker inspect`, to anyone with the Unraid web UI, and is written in clear text to
> `/boot/config/plugins/dockerMan/templates-user/*.xml` on the flash drive — which is what the
> Unraid flash backup copies. This keystore is **irreplaceable**: without it, an installed
> `org.xbmc.fandangos` can never be updated again, only uninstalled and reinstalled, losing that
> app's data. If that trade is not acceptable, mount the keystore read-only instead and set
> `KODI_ANDROID_STORE_FILE`:
>
> ```
> -v /mnt/user/appdata/kodi-keys/fandangos-release.keystore:/keys/release.keystore:ro
> -e KODI_ANDROID_STORE_FILE=/keys/release.keystore
> ```
>
> Either way, keep a copy of the keystore somewhere that is not this machine.

## Unraid

The web UI's *Add Container* always runs a `docker pull`, so a template pointing at an image
that exists only in the box's local image store fails at Apply. Two routes:

### Web UI (recommended)

Runs a small registry on the Unraid box itself and pushes into it over the LAN, which makes the
templates behave exactly like any Community Applications container:

```bash
./deploy-to-unraid.sh <unraid-host> --registry --key ~/.android/fandangos-release.keystore
```

That starts `registry:2` on the Unraid box if it isn't already there, pushes both tags into it
through an ssh tunnel, installs the two templates into
`/boot/config/plugins/dockerMan/templates-user/`, and copies the signing key into each
container's `keys/` directory. Then: **Docker → Add Container → Template →
`kodi-build-arm64-v8a`**, check the data directory, Apply. Repeat for `kodi-build-armeabi-v7a`.

The push goes through an ssh tunnel so this machine's Docker sees `localhost:5000`, which it
treats as an insecure registry by default — pushing to `<host>:5000` directly would require
adding `insecure-registries` to the daemon config here for no benefit.

Cost: one always-running 25 MB container and ~1 GB of registry storage on the Unraid box.

Re-running the script later updates the images in place — but **`deploy-to-unraid.sh` only
ships, it does not build**. Run `docker build` (or `publish.sh`) first, or it will faithfully
ship the images you already had and nothing will change. The cheap proof that the right thing
arrived, run on the Unraid box:

```sh
docker pull localhost:5000/kodi-android-builder:armeabi-v7a   # Downloaded newer image?
docker run --rm --entrypoint sh localhost:5000/kodi-android-builder:armeabi-v7a \
  -c "grep -c 'Evicting cached libbluray' /usr/local/bin/build-kodi-android"
```

To pick the new image up: click the container → **Edit** → **Apply**. That recreates it against
the pulled image; `/data` is a bind mount, so `src/`, `state/` and `release/` — the SDK, NDK,
tarballs, gradle cache and the whole depends tree — all survive untouched.

**Start alone does nothing here**: the container was created from the old image ID and
`docker start` reuses it. Neither does *Check for Updates* — that check queries Docker Hub's
API and cannot read a private `localhost:5000` registry, so it reports `not available` with a
broken-image icon. That is the expected answer for this setup, not a fault to debug.

Every ssh, scp and the registry port forward share one connection, so a box without key auth
asks for the password once. To stop being asked at all, note that Unraid's `/root` is a tmpfs:
`ssh-copy-id` works but does not survive a reboot. The persistent location for a public key is
`/boot/config/ssh/root.pubkeys`, which Unraid installs into `/root/.ssh/authorized_keys` at boot.

### No registry

```bash
./deploy-to-unraid.sh <unraid-host> --key ~/.android/fandangos-release.keystore
```

`docker save | ssh | docker load`, then create the containers once from the Unraid terminal with
the command the script prints. They appear in the Docker tab afterwards with working
Start/Stop/Logs; only editing settings needs the terminal (`docker rm` and re-create).

### The signing key

`--key` copies the keystore and, if one sits next to it, its `.pass` file into each container's
`keys/` directory over ssh — the password is never printed and never becomes an environment
variable. The build then picks it up automatically: if `KODI_ANDROID_STORE_PASSWORD` is empty it
reads `<keystore>.pass` from beside the keystore, stripping any trailing newline (a `.pass` file
written with `echo` has one, and keytool rejects a password that looks right to a human).

Leave the password field in the template empty when you do this. Filling it in writes the
password in clear text into the template XML on the flash drive, which is what the flash backup
copies.

## Publishing to Docker Hub

```bash
./publish.sh <dockerhub-user>          # builds both tags and pushes
./publish.sh <dockerhub-user> v1       # also pushes :arm64-v8a-v1 / :armeabi-v7a-v1
```

`docker login` first. The build needs network access to Google (SDK/NDK), GitHub (JREs) and
mirrors.kodi.tv (libbluray).

## The 32-bit build: read this before trusting an armeabi-v7a APK

The container removes the two blockers that made a 32-bit build impossible, but it cannot
remove the third.

1. **JRE** — solved. The `armeabi-v7a` image carries the 32-bit OpenJDK 8 build from the
   `script.service.jre` addon's `1.2` release, whose `libjvm.so` sits under `lib/arm/`.
2. **BD-J jars** — solved, and this was an unlisted trap: the 32-bit JRE zip ships the 2022 jars
   named `libbluray-*-j2se-1.3.2.jar`, while libbluray 1.5.0 looks for `-1.5.0` by exact name and
   would silently find nothing. The image deletes the shipped jars and installs freshly built
   1.5.0 ones in both trees.
3. **`005-aarch64-java-arch.patch` needs no arch guard.** The earlier handoff said this patch had
   to be prevented from applying to 32-bit builds. It does not: the patch rewrites only the
   `aarch64` branch of libbluray's `java_arch` conditional, and a 32-bit build takes the `else`
   branch, which yields `arm` — already correct for the 32-bit JRE's layout. Applying it is a
   no-op. Nothing to do.
4. **Everything else in this fork was developed and tested on 64-bit Android TV hardware only.**
   Dolby Vision (libdovi, the profile 7→8.1 merge), the MediaCodec DOVI path and the HDR GUI
   surface have never run on a 32-bit device. A 32-bit APK will build; whether those features
   work on one is unknown.

## Verification

At the end of a build the container checks the artifact and fails the run if any of these are
wrong:

- `CMakeCache.txt` really contains the requested `APP_PACKAGE` and `CMAKE_BUILD_TYPE` — checked
  *before* compiling, because both silently fall back to the wrong value
  (`org.xbmc.kodi` / `Debug`)
- `aapt2 dump packagename` matches `KODI_APP_PACKAGE`
- the APK is not signed with a debug key when `KODI_BUILD_CONFIG=Release`
- `assets/j2re-image` contains a `libjvm.so` **and** a `libbluray-j2se-*.jar` — without both,
  BD-J menus fail at runtime with nothing obvious in the log
- the `lib/<arch>/{server,client}` paths compiled into the shipped `libkodi.so` resolve to a
  real `libjvm.so` in the shipped JRE. Both halves being present is not the same as them
  agreeing: the `<arch>` component is frozen into libbluray at compile time, and a JRE laid
  out under a different directory name is indistinguishable, from Kodi's side, from no JRE at
  all. This is checked binary-against-binary, because every cheaper form of the check
  (reading the source, the cmake, the arch name) has already been fooled once — see below

### Two source bugs this container exposed (both fixed, both outside `tools/docker/`)

Building from a genuinely clean tree is not the same as building on a workstation that has been
iterated on for a week, and it found two real defects. Neither is caused by the container.

1. **`cmake/modules/FindBluray.cmake` did not apply patches 005 and 006.** libbluray is *not* in
   the depends `DEPENDS` list — Kodi builds it as an internal CMake ExternalProject, and that
   build has its own patch list, which had 001–004 and `tvos` but neither the aarch64 `JAVA_ARCH`
   fix nor the HDMV BC compare fix. The workstation never noticed because someone had built
   `tools/depends/target/libbluray` by hand, so a patched libbluray was already sitting in the
   depends prefix and CMake used that instead. From clean, the arm64 APK linked an **unpatched**
   libbluray that searched `lib/arm/server` for `libjvm.so` while the bundled JRE puts it in
   `lib/aarch64/server` — BD-J menus would never have started.
2. **`cmake/scripts/common/ModuleHelpers.cmake` mapped no `cpu_family` for `armeabi-v7a`.** The
   CMake→meson CPU mapping matches `ARMV.`, and Android's CPU string is `armeabi-v7a`, which
   contains no `ARMV`. `cpu_family` was written to every generated meson cross file as the empty
   string; meson accepts that without complaint, and libbluray derived `JAVA_ARCH=""` and searched
   `lib//server`. This affects *every* internal meson dependency on 32-bit ARM, not just
   libbluray, and is a plausible part of why 32-bit was never made to work.

Both are verified by `#define JAVA_ARCH` in the generated `config.h` and by the search-path
strings in the finished `libkodi.so`: `lib/aarch64/server` on arm64, `lib/arm/client` on
armeabi-v7a, each matching the JRE bundled in the same APK.

> [!NOTE]
> A consequence worth knowing: the internal builds **install into the depends prefix**, so a
> library already there wins over a rebuild. After changing a patch under
> `tools/depends/target/<pkg>/`, delete that library from `/state/xbmc-depends/<host>-<api>-<type>/`
> (`lib/lib<pkg>.a`, `lib/pkgconfig/<pkg>.pc`, `include/<pkg>`) or the build will quietly keep
> using the old one. For libbluray specifically the container now does this itself: before
> configuring, it greps the cached `libbluray.a` for the JVM search path it should have and
> evicts it (plus `build/build-libbluray` and the ExternalProject stamp) if it does not match.
> `FindBluray.cmake` runs `SEARCH_EXISTING_PACKAGES()` *before* it decides to build, so a
> wrong libbluray is not merely reused once — it is reused forever, and no source or cmake
> change makes it reconsider.

> [!CAUTION]
> **A fix that is not pushed does not exist, and the container will not tell you.** Both source
> bugs above were found, fixed and written up on 2026-08-12 — while the fixes themselves sat
> *uncommitted* in the workstation's working tree until 2026-08-12 late afternoon. The Unraid
> container clones from `KODI_GIT_URL`, so it built the unfixed cmake and produced an
> armeabi-v7a APK that installed cleanly, launched, and failed on the first BD-J disc with
> "Could not load the java vm". Measured on the APK pulled back off the TV: `libkodi.so`
> carries `lib//server` and `lib//client`, i.e. `JAVA_ARCH=""`, against a bundled JRE with
> `lib/arm/client/libjvm.so`.
>
> The `JVM search path` row of the table below is therefore **not** evidence about any artifact
> that reached a device, and how it came to read `lib/arm/client` was never established — treat
> it as unverified. The arm64 row is equally suspect for the same reason: without patch 005 a
> clean clone maps aarch64 to `JAVA_ARCH="arm"`, and the aarch64 JRE has only `lib/aarch64`.
> **Check the arm64 APK on a BD-J disc before trusting it.**
>
> Before pressing Start, confirm the fix is on the remote — `git status` clean and
> `git log origin/<branch> -1` showing it — not merely in your editor. The build-time guard
> added above now catches this class at the container, but only once the *image* carrying it
> has been republished; the entrypoint is baked in, not read from the source tree.

### What has actually been run

On 2026-08-11 **both** images were taken through a full cold build — depends from an empty volume,
Kodi, `make apk` — against throwaway clones of this branch at `306287c`:

| | arm64-v8a | armeabi-v7a |
|---|---|---|
| APK | 117 MB, signed, `org.xbmc.fandangos` | 118 MB, signed, `org.xbmc.fandangos` |
| `libkodi.so` | ELF 64-bit aarch64, NDK r28c | ELF 32-bit ARM EABI5, NDK r28c |
| JVM search path | `lib/aarch64/server` | `lib/arm/client` |
| bundled JRE | `lib/aarch64/server/libjvm.so` | `lib/arm/client/libjvm.so` |
| BD-J jars | `libbluray-j2se-1.5.0.jar` | `libbluray-j2se-1.5.0.jar` |

The failure paths (wrong store password, missing key on a Release build, invalid ABI) were
exercised too. **Neither APK was installed or launched** — no device was attached to these runs,
and the 32-bit build has never run on 32-bit hardware at all.

> [!IMPORTANT]
> None of that proves the APK works. Install it and **launch** it. Two APKs that passed package
> name and signature checks were handed over on 2026-08-11 without being started: the first
> aborted instantly in `JNI_OnLoad`, the second could not play a disc.

## Notes

- Ubuntu 24.04 is pinned deliberately, and this is the reason. `tools/depends/bootstrap`
  regenerates `configure` with the **host** autoconf. From 2.72, `AC_PROG_CC` appends the
  detected C standard flag to `CC` itself, so `$CC` becomes
  `.../aarch64-linux-android24-clang -std=gnu23`. `configure.ac` then writes that straight into
  the meson cross-file (`MESON_CC="'$CC'"`, or `['$CCACHE', '$CC']` with ccache), and meson tries
  to exec a path containing a space and a flag. That is the whole first half of
  `KODI_ANDROID_BUILD_HANDBOOK.md` — and it is a property of the *build host*, not of Kodi.
  Ubuntu 24.04 ships autoconf 2.71, so it does not happen here; verified by inspecting the
  generated `config.site` and `cross-file.meson` from a real run in this image, where `CC` is a
  bare compiler path. None of the handbook's repair scripts are needed and none are included.
  Bumping the base image to something with autoconf ≥ 2.72 brings all of them back.
  (For the record: the trailing `$CFLAGS` / `$LDFLAGS` in `config.site` that the handbook also
  strips are *not* a defect. `config.site` is a shell script that `configure` sources, so those
  expand to any pre-existing flags — upstream wrote them on purpose.)
- The handbook's samba `PERLMODULE` fix is likewise not needed: it works around a NixOS perl
  layout, and upstream's stock `find $(NATIVEPREFIX)/share/` search is correct on Ubuntu.
- **Do not point `/src` at a checkout that a host build also uses.** `tools/depends/configure`
  rewrites `Makefile.include`, `target/config.site` and `target/Toolchain.cmake` in the tree with
  the container's paths, and the depends packages build in-tree under
  `tools/depends/target/*/<host>-<api>-<type>/` — the same directory name a host build uses. A
  shared checkout means the two builds silently overwrite each other. Give the container its own
  clone.
- The depends prefix lives at a fixed container path (`/state/xbmc-depends`) regardless of where
  the host volume is, because that path is baked into hundreds of generated files. Moving the
  host directory is fine; changing the container path is not.
- `JRE_IMAGE_DIR` is exported rather than copied into the tree, so the source checkout is never
  modified by the container.
