#!/usr/bin/env bash
#
# Kodi Android build entrypoint.
#
#   depends -> cmakebuildsys -> make -> make apk -> verify -> /output
#
# Every knob is an environment variable so the whole thing is drivable from an
# Unraid template. See README.md for the full list.

set -Eeuo pipefail

log()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }

# Errors go to STDOUT, deliberately. The log is this container's entire user
# interface, and Unraid's log viewer collects stderr separately - it shows those
# lines detached from the output they belong to, usually at the top, which reads
# as "it died silently" and sends you looking in the wrong place. The exit code
# still reports the failure; only the stream changes.
die()  { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*"; exit 1; }

# Anything that fails WITHOUT going through die() - a command that just returns
# non-zero under set -e - would otherwise stop the log mid-sentence with no
# explanation at all. Name the line and the command instead.
on_error() {
  local rc=$1 line=$2 cmd=$3
  printf '\n\033[1;31mFAILED (exit %d) at line %d: %s\033[0m\n' "${rc}" "${line}" "${cmd}"
}
trap 'on_error $? $LINENO "$BASH_COMMAND"' ERR

# ---------------------------------------------------------------------------
# Paths. Resolved before the privilege drop so both passes agree on them.
#
# One mount instead of three: KODI_DATA=/data puts the checkout, the build state
# and the finished APKs under a single directory, which is how this ends up on
# an Unraid box with one appdata path per container. Any of the three can still
# be pointed elsewhere individually.
# ---------------------------------------------------------------------------
if [[ -n "${KODI_DATA:-}" ]]; then
  KODI_SRC="${KODI_SRC:-${KODI_DATA}/src}"
  KODI_STATE="${KODI_STATE:-${KODI_DATA}/state}"
  KODI_OUTPUT="${KODI_OUTPUT:-${KODI_DATA}/release}"
fi
export KODI_SRC="${KODI_SRC:-/src}"
export KODI_STATE="${KODI_STATE:-/state}"
export KODI_OUTPUT="${KODI_OUTPUT:-/output}"

# ---------------------------------------------------------------------------
# Drop privileges to PUID/PGID (Unraid: 99:100) so nothing lands on the array
# owned by root. Re-execs itself once, then continues as the build user.
# ---------------------------------------------------------------------------
if [[ "$(id -u)" == "0" ]]; then
  PUID="${PUID:-99}"
  PGID="${PGID:-100}"

  if ! getent group "${PGID}" >/dev/null; then
    groupadd -g "${PGID}" kodibuild
  fi
  if ! getent passwd "${PUID}" >/dev/null; then
    # Output captured and only shown on failure. Unraid's nobody is uid 99,
    # below the distro's UID_MIN of 1000, so useradd emits a warning that is
    # purely cosmetic here - and a warning at the top of every build log is a
    # warning people learn to ignore.
    if ! useradd_out="$(useradd -u "${PUID}" -g "${PGID}" -M \
                                -d "${KODI_STATE}/home" -s /bin/bash kodibuild 2>&1)"; then
      printf '%s\n' "${useradd_out}" >&2
      die "could not create the build user (uid ${PUID}, gid ${PGID})"
    fi
  fi

  # Slim image: no SDK baked in. Point $ANDROID_HOME at the persistent volume
  # via a symlink, so the path is /opt/android-sdk in both variants. That
  # matters more than it looks - `tools/depends/configure` writes absolute NDK
  # paths into config.site, Toolchain.cmake and cross-file.meson, so an SDK that
  # moves between runs silently invalidates a configured depends tree.
  if [[ ! -e "${ANDROID_HOME:-/opt/android-sdk}" ]]; then
    mkdir -p "${KODI_STATE}/android-sdk"
    chown "${PUID}:${PGID}" "${KODI_STATE}/android-sdk" 2>/dev/null || true
    ln -sfn "${KODI_STATE}/android-sdk" "${ANDROID_HOME:-/opt/android-sdk}"
  fi

  # KODI_SRC is in this list because of KODI_DATA: with three separate mounts
  # /src always existed already, but /data/src has to be created here, as root,
  # before the build user could ever write into a root-owned data directory.
  for d in "${KODI_SRC}" "${KODI_STATE}" "${KODI_OUTPUT}" "${KODI_STATE}/home"; do
    mkdir -p "$d"
    # Top level only. A recursive chown over a populated depends tree costs
    # minutes on a fuse/array mount and is not needed - files created by this
    # user already carry the right ownership.
    chown "${PUID}:${PGID}" "$d" 2>/dev/null || true
  done

  exec setpriv --reuid="${PUID}" --regid="${PGID}" --clear-groups \
       env HOME="${KODI_STATE}/home" "$0" "$@"
fi

# ---------------------------------------------------------------------------
# Settings
# ---------------------------------------------------------------------------
KODI_ARCH="${KODI_ARCH:-arm64-v8a}"
KODI_APP_PACKAGE="${KODI_APP_PACKAGE:-org.xbmc.fandangos}"
KODI_BUILD_CONFIG="${KODI_BUILD_CONFIG:-Release}"
KODI_NDK_API="${KODI_NDK_API:-24}"
KODI_DEPENDS_DEBUG="${KODI_DEPENDS_DEBUG:-yes}"
KODI_JOBS="${KODI_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
KODI_ENABLE_CCACHE="${KODI_ENABLE_CCACHE:-0}"
KODI_BUNDLE_BDJ="${KODI_BUNDLE_BDJ:-1}"
KODI_RECONFIGURE="${KODI_RECONFIGURE:-0}"
KODI_SKIP_DEPENDS="${KODI_SKIP_DEPENDS:-0}"
KODI_ALLOW_DEBUG_KEY="${KODI_ALLOW_DEBUG_KEY:-0}"
KODI_GIT_URL="${KODI_GIT_URL:-}"
KODI_GIT_REF="${KODI_GIT_REF:-}"
KODI_GIT_PULL="${KODI_GIT_PULL:-0}"
KODI_GIT_REMOTE="${KODI_GIT_REMOTE:-origin}"
KODI_GIT_RESET="${KODI_GIT_RESET:-0}"
KODI_EXTRA_CMAKE_ARGS="${KODI_EXTRA_CMAKE_ARGS:-}"

case "${KODI_ARCH}" in
  arm64-v8a)
    HOST_TRIPLE=aarch64-linux-android
    ;;
  armeabi-v7a)
    HOST_TRIPLE=arm-linux-androideabi
    ;;
  *)
    die "KODI_ARCH must be arm64-v8a or armeabi-v7a (got '${KODI_ARCH}')"
    ;;
esac

case "${KODI_BUILD_CONFIG}" in
  Release|Debug|RelWithDebInfo) ;;
  *) die "KODI_BUILD_CONFIG must be Release, Debug or RelWithDebInfo (got '${KODI_BUILD_CONFIG}')" ;;
esac

DEPENDS_BUILD_TYPE=debug
DEPENDS_DEBUG_FLAG=--enable-debug
if [[ "${KODI_DEPENDS_DEBUG}" != "yes" ]]; then
  DEPENDS_BUILD_TYPE=release
  DEPENDS_DEBUG_FLAG=--disable-debug
fi

PREFIX="${KODI_STATE}/xbmc-depends"
TARBALLS="${KODI_STATE}/tarballs"
DEPS_DIR="${HOST_TRIPLE}-${KODI_NDK_API}-${DEPENDS_BUILD_TYPE}"
NATIVEPREFIX="${PREFIX}/x86_64-linux-gnu-native"
BUILD_DIR="${KODI_SRC}/build-${KODI_ARCH}-$(echo "${KODI_BUILD_CONFIG}" | tr 'A-Z' 'a-z')"
JRE_IMAGE_DIR="/opt/kodi-jre/${KODI_ARCH}/j2re-image"

export GRADLE_USER_HOME="${KODI_STATE}/gradle"
export CCACHE_DIR="${KODI_STATE}/ccache"
WORKHOME="${KODI_STATE}/home"
export HOME="${WORKHOME}"
mkdir -p "${TARBALLS}" "${GRADLE_USER_HOME}" "${KODI_OUTPUT}" "${WORKHOME}"

# The source is bind-mounted and will not be owned by this uid. Without this,
# git refuses to read it ("dubious ownership") and the version baked into
# CompileInfo.cpp silently degrades.
git config --global --add safe.directory '*' 2>/dev/null || true

log "Kodi Android builder"
info "ABI            : ${KODI_ARCH}  (--host=${HOST_TRIPLE})"
info "package        : ${KODI_APP_PACKAGE}"
info "configuration  : ${KODI_BUILD_CONFIG}   (depends: ${DEPENDS_BUILD_TYPE})"
info "source         : ${KODI_SRC}"
info "build dir      : ${BUILD_DIR}"
info "depends prefix : ${PREFIX}/${DEPS_DIR}"
info "jobs           : ${KODI_JOBS}"
info "NDK            : ${ANDROID_NDK_VERSION:-unknown}"

# Checked before the SDK download and the clone on purpose: a wrong alias or an
# unreadable keystore is the cheapest possible failure, and finding out about it
# after three gigabytes and a git clone is not.
# ---------------------------------------------------------------------------
# Signing key
#
# Two ways in: a base64 blob in the environment (Unraid template friendly), or
# a keystore file mounted into the container. The blob wins if both are set.
# ---------------------------------------------------------------------------
setup_signing() {
  set +x  # never trace this function

  if [[ -n "${KODI_ANDROID_KEYSTORE_B64:-}" ]]; then
    # Container-local, not on the persistent volume: the decoded key dies with
    # the container rather than living on the array.
    local dir
    dir="$(mktemp -d /tmp/kodi-signing.XXXXXX)"
    chmod 700 "${dir}"
    printf '%s' "${KODI_ANDROID_KEYSTORE_B64}" | tr -d ' \n\r\t' | base64 -d > "${dir}/release.keystore" \
      || die "KODI_ANDROID_KEYSTORE_B64 is not valid base64"
    chmod 600 "${dir}/release.keystore"
    [[ -s "${dir}/release.keystore" ]] || die "KODI_ANDROID_KEYSTORE_B64 decoded to an empty file"
    export KODI_ANDROID_STORE_FILE="${dir}/release.keystore"
    info "signing key    : from KODI_ANDROID_KEYSTORE_B64"
  elif [[ -n "${KODI_ANDROID_STORE_FILE:-}" ]]; then
    if [[ ! -r "${KODI_ANDROID_STORE_FILE}" ]]; then
      # Order matters. A keys directory that this uid cannot traverse makes even
      # `-e` on the file false, so "does not exist" would be a lie - and a lie
      # that sends you looking in the wrong place.
      local key_dir="$(dirname "${KODI_ANDROID_STORE_FILE}")"
      if [[ ! -x "${key_dir}" ]]; then
        die "cannot look inside ${key_dir} as uid $(id -u):$(id -g).
       It is $(stat -c '%U:%G mode %a' "${key_dir}" 2>/dev/null || echo 'not accessible').
       A keystore copied in as root is the usual cause. On Unraid:
         chown -R nobody:users $(dirname "${key_dir}")"
      elif [[ -e "${KODI_ANDROID_STORE_FILE}" ]]; then
        die "${KODI_ANDROID_STORE_FILE} exists but is not readable by uid $(id -u):$(id -g).
       It is $(stat -c '%U:%G mode %a' "${KODI_ANDROID_STORE_FILE}" 2>/dev/null).
       On Unraid: chown -R nobody:users $(dirname "${key_dir}")"
      else
        die "KODI_ANDROID_STORE_FILE=${KODI_ANDROID_STORE_FILE} does not exist"
      fi
    fi
    info "signing key    : ${KODI_ANDROID_STORE_FILE}"
  else
    if [[ "${KODI_BUILD_CONFIG}" == "Release" && "${KODI_ALLOW_DEBUG_KEY}" != "1" ]]; then
      die "No signing key. A Release APK signed with a throwaway debug key cannot update an
       installed ${KODI_APP_PACKAGE} (signature mismatch) and cannot be handed out.
       Set KODI_ANDROID_KEYSTORE_B64 (+ alias/passwords), or mount a keystore and set
       KODI_ANDROID_STORE_FILE, or set KODI_ALLOW_DEBUG_KEY=1 to accept a debug key."
    fi
    local dbg="${WORKHOME}/.android/debug.keystore"
    if [[ ! -f "${dbg}" ]]; then
      log "Generating a throwaway debug keystore"
      mkdir -p "${WORKHOME}/.android"
      keytool -genkey -keystore "${dbg}" -v -alias androiddebugkey \
        -dname "CN=Android Debug,O=Android,C=US" -keypass android -storepass android \
        -keyalg RSA -keysize 2048 -validity 10000 >/dev/null
    fi
    export KODI_ANDROID_STORE_FILE="${dbg}"
    export KODI_ANDROID_STORE_PASSWORD=android
    export KODI_ANDROID_KEY_PASSWORD=android
    export KODI_ANDROID_KEY_ALIAS=androiddebugkey
    info "signing key    : throwaway debug key"
  fi

  # Passwords from a file rather than the environment. An explicit
  # *_PASSWORD_FILE wins; otherwise a "<keystore>.pass" sitting next to the
  # keystore is picked up automatically, which is the layout keytool users
  # already have. Nothing then appears in `docker inspect` or a template XML.
  local pw_file
  if [[ -z "${KODI_ANDROID_STORE_PASSWORD:-}" ]]; then
    for pw_file in "${KODI_ANDROID_STORE_PASSWORD_FILE:-}" "${KODI_ANDROID_STORE_FILE}.pass"; do
      if [[ -n "${pw_file}" && -r "${pw_file}" ]]; then
        # Trailing newlines are the classic failure here: a password file made
        # with `echo` carries one, and keytool then rejects a password that
        # looks correct to a human.
        KODI_ANDROID_STORE_PASSWORD="$(tr -d '\r\n' < "${pw_file}")"
        info "store password : read from $(basename "${pw_file}")"
        break
      fi
    done
  fi
  if [[ -z "${KODI_ANDROID_KEY_PASSWORD:-}" && -n "${KODI_ANDROID_KEY_PASSWORD_FILE:-}" ]]; then
    [[ -r "${KODI_ANDROID_KEY_PASSWORD_FILE}" ]] \
      || die "KODI_ANDROID_KEY_PASSWORD_FILE=${KODI_ANDROID_KEY_PASSWORD_FILE} is not readable"
    KODI_ANDROID_KEY_PASSWORD="$(tr -d '\r\n' < "${KODI_ANDROID_KEY_PASSWORD_FILE}")"
  fi

  export KODI_ANDROID_KEY_ALIAS="${KODI_ANDROID_KEY_ALIAS:-androiddebugkey}"
  export KODI_ANDROID_STORE_PASSWORD="${KODI_ANDROID_STORE_PASSWORD:-android}"
  # Most keystores use one password for store and key; default the key password
  # to the store password rather than to "android", which would fail late.
  export KODI_ANDROID_KEY_PASSWORD="${KODI_ANDROID_KEY_PASSWORD:-${KODI_ANDROID_STORE_PASSWORD}}"

  # Fail now, not 45 minutes from now in Gradle.
  if ! keytool -list -keystore "${KODI_ANDROID_STORE_FILE}" \
        -storepass "${KODI_ANDROID_STORE_PASSWORD}" -alias "${KODI_ANDROID_KEY_ALIAS}" >/dev/null 2>&1; then
    die "Cannot open alias '${KODI_ANDROID_KEY_ALIAS}' in the keystore - wrong alias or store password."
  fi
  info "key alias      : ${KODI_ANDROID_KEY_ALIAS} (verified)"
}
setup_signing

# ---------------------------------------------------------------------------
# Android SDK / NDK
#
# A no-op on the fat image. On the slim one this installs the same pinned
# components into /state/android-sdk on first run. Deliberately checks for the
# directories rather than asking sdkmanager: sdkmanager always fetches the
# remote repository index, so a "nothing to do" run would still need network,
# and every subsequent build would depend on Google being reachable.
# ---------------------------------------------------------------------------
NDK_DIR="${ANDROID_HOME}/ndk/${ANDROID_NDK_VERSION}"
sdk_missing=()
[[ -d "${NDK_DIR}" ]]                                          || sdk_missing+=("ndk;${ANDROID_NDK_VERSION}")
[[ -d "${ANDROID_HOME}/platforms/${ANDROID_PLATFORM}" ]]       || sdk_missing+=("platforms;${ANDROID_PLATFORM}")
[[ -d "${ANDROID_HOME}/build-tools/${ANDROID_BUILD_TOOLS}" ]]  || sdk_missing+=("build-tools;${ANDROID_BUILD_TOOLS}")
[[ -d "${ANDROID_HOME}/build-tools/${ANDROID_AGP_BUILD_TOOLS}" ]] || sdk_missing+=("build-tools;${ANDROID_AGP_BUILD_TOOLS}")
[[ -d "${ANDROID_HOME}/platform-tools" ]]                      || sdk_missing+=("platform-tools")

if [[ ${#sdk_missing[@]} -gt 0 ]]; then
  log "Installing Android SDK components (${#sdk_missing[@]} missing, ~3 GB, first run only)"
  info "into ${ANDROID_HOME} -> $(readlink -f "${ANDROID_HOME}")"
  yes | sdkmanager --sdk_root="${ANDROID_HOME}" --licenses > /dev/null 2>&1 || true
  sdkmanager --sdk_root="${ANDROID_HOME}" --install "${sdk_missing[@]}" \
    || die "SDK install failed. This image ships without the SDK and fetches it once per state
       volume; it needs to reach dl.google.com. Use the full image if the box has no
       outbound access."
  rm -rf "${ANDROID_HOME}/.temp"
  info "installed: ${sdk_missing[*]}"
fi

[[ -x "${NDK_DIR}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" ]] \
  || die "NDK ${ANDROID_NDK_VERSION} is not usable at ${NDK_DIR}"

# tools/depends/configure insists on finding sdkmanager INSIDE the SDK - it
# tests $SDK/tools/bin, $SDK/cmdline-tools/bin and $SDK/cmdline-tools/latest/bin
# and stops with "verify sdk path" otherwise. cmdline-tools lives at
# /opt/cmdline-tools so that the slim variant can host the SDK on /state, so
# link it back into place. Cheap, idempotent, and needed in both variants.
if [[ ! -f "${ANDROID_HOME}/cmdline-tools/latest/bin/sdkmanager" ]]; then
  mkdir -p "${ANDROID_HOME}/cmdline-tools"
  ln -sfn /opt/cmdline-tools/latest "${ANDROID_HOME}/cmdline-tools/latest"
fi
[[ -f "${ANDROID_HOME}/cmdline-tools/latest/bin/sdkmanager" ]] \
  || die "sdkmanager is not visible at ${ANDROID_HOME}/cmdline-tools/latest/bin - depends configure would stop with 'verify sdk path'"

# ---------------------------------------------------------------------------
# Source
# ---------------------------------------------------------------------------
if [[ ! -f "${KODI_SRC}/version.txt" ]]; then
  if [[ -n "${KODI_GIT_URL}" ]]; then
    log "Cloning ${KODI_GIT_URL}${KODI_GIT_REF:+ (${KODI_GIT_REF})}"
    mkdir -p "${KODI_SRC}"
    [[ -z "$(ls -A "${KODI_SRC}")" ]] || die "${KODI_SRC} is not empty and is not a Kodi checkout"
    git clone ${KODI_GIT_REF:+--branch "${KODI_GIT_REF}"} "${KODI_GIT_URL}" "${KODI_SRC}"
  else
    die "${KODI_SRC}/version.txt not found - mount the Kodi source at ${KODI_SRC}, or set KODI_GIT_URL"
  fi
fi

[[ -w "${KODI_SRC}" ]] || die "${KODI_SRC} is not writable - depends builds in-tree and needs write access"

# Pick up new commits without touching a terminal: this is what makes "push to
# GitHub, press Start" work. Never `git clean` here - the depends packages and
# the build directory live inside the source tree as untracked files, and
# deleting them turns a 10 minute incremental build into an hour.
if [[ "${KODI_GIT_PULL}" == "1" ]]; then
  log "Updating the checkout"
  cd "${KODI_SRC}"
  git rev-parse --git-dir >/dev/null 2>&1 || die "KODI_GIT_PULL=1 but ${KODI_SRC} is not a git repository"
  git remote get-url "${KODI_GIT_REMOTE}" >/dev/null 2>&1 \
    || die "no git remote named '${KODI_GIT_REMOTE}' in ${KODI_SRC} - set KODI_GIT_REMOTE. Remotes: $(git remote | tr '\n' ' ')"

  if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    if [[ "${KODI_GIT_RESET}" == "1" ]]; then
      info "tracked files are modified - KODI_GIT_RESET=1, discarding those changes"
    else
      die "tracked files in ${KODI_SRC} are modified. Commit or revert them, or set
       KODI_GIT_RESET=1 to discard local modifications on every build."
    fi
  fi

  git fetch --prune "${KODI_GIT_REMOTE}" \
    || die "git fetch from '${KODI_GIT_REMOTE}' ($(git remote get-url "${KODI_GIT_REMOTE}")) failed.
       A container cannot reach a remote that is a path on the host, and it has no ssh keys -
       use an https:// URL. Set KODI_GIT_PULL=0 to build what is on disk instead."
  REF="${KODI_GIT_REF:-$(git rev-parse --abbrev-ref HEAD)}"
  if [[ "${KODI_GIT_RESET}" == "1" ]]; then
    git checkout -B "${REF}" "${KODI_GIT_REMOTE}/${REF}"
    git reset --hard "${KODI_GIT_REMOTE}/${REF}"
  else
    git checkout "${REF}"
    git merge --ff-only "${KODI_GIT_REMOTE}/${REF}" \
      || die "cannot fast-forward ${REF} onto ${KODI_GIT_REMOTE}/${REF} - the local branch has diverged.
       Set KODI_GIT_RESET=1 to make the build always match the remote."
  fi
  info "now at $(git log -1 --format='%h %s' | cut -c1-72)"
  cd "${KODI_SRC}"
fi

SRC_DESC="$(cd "${KODI_SRC}" && git describe --always --dirty 2>/dev/null || echo nogit)"
SRC_DESC="${SRC_DESC//\//-}"
info "source rev     : ${SRC_DESC}"


# ---------------------------------------------------------------------------
# BD-J JRE
#
# JRE_IMAGE_DIR is a `?=` in tools/android/packaging/Makefile.in, so exporting
# it here overrides the default without touching the source tree.
# ---------------------------------------------------------------------------
if [[ "${KODI_BUNDLE_BDJ}" == "1" ]]; then
  [[ -d "${JRE_IMAGE_DIR}" ]] || die "No BD-J JRE for ${KODI_ARCH} at ${JRE_IMAGE_DIR}"

  # Work on a copy, not on the image's own tree.  The BD-J jars inside it get
  # rebuilt below from our libbluray patches, and this container is long lived
  # on Unraid - patching the image copy in place would leave the last run's
  # classes behind if a patch were later dropped.  Refreshed every run so the
  # base is always the jars the image shipped.
  BDJ_JRE_PRISTINE="${JRE_IMAGE_DIR}"
  JRE_IMAGE_DIR="${KODI_STATE}/bdj-jre/${KODI_ARCH}/j2re-image"
  rm -rf "${KODI_STATE}/bdj-jre/${KODI_ARCH}"
  mkdir -p "$(dirname "${JRE_IMAGE_DIR}")"
  cp -a "${BDJ_JRE_PRISTINE}" "${JRE_IMAGE_DIR}"
  export JRE_IMAGE_DIR
  info "BD-J JRE       : ${JRE_IMAGE_DIR} (copy of ${BDJ_JRE_PRISTINE})"

  # The directory libbluray will look in for libjvm.so. It is baked into the
  # library at compile time (JAVA_ARCH, derived from the meson cross file's
  # cpu_family) and must match the layout of the JRE image bundled above:
  #   arm64-v8a   -> lib/aarch64   (needs 005-aarch64-java-arch.patch; libbluray
  #                                 upstream maps aarch64 to 'arm')
  #   armeabi-v7a -> lib/arm       (libbluray's stock mapping, but only if
  #                                 cpu_family reaches meson as 'arm' at all)
  case "${KODI_ARCH}" in
    arm64-v8a)   BDJ_JAVA_ARCH=aarch64 ;;
    armeabi-v7a) BDJ_JAVA_ARCH=arm ;;
  esac
else
  # Point at a path that does not exist; the packaging Makefile warns and skips.
  export JRE_IMAGE_DIR=/nonexistent-bdj-disabled
  info "BD-J JRE       : disabled (KODI_BUNDLE_BDJ=0) - no disc menus in this APK"
fi

# ---------------------------------------------------------------------------
# depends
# ---------------------------------------------------------------------------
cd "${KODI_SRC}/tools/depends"

if [[ "${KODI_SKIP_DEPENDS}" != "1" ]]; then
  if [[ ! -x ./configure ]]; then
    log "Bootstrapping depends"
    ./bootstrap
  fi

  CONFIG_STAMP="${PREFIX}/${DEPS_DIR}/.kodi-builder-configured"

  # tools/depends/Makefile.include holds ONE host triple, so a source tree that
  # was last configured for the other ABI must be reconfigured even though this
  # ABI's stamp exists. Without this check a second container sharing the same
  # /src builds the wrong architecture without saying anything.
  TREE_HOST="$(awk -F= '/^HOST=/{print $2; exit}' Makefile.include 2>/dev/null || true)"
  RECONFIGURE_REASON=""
  [[ "${KODI_RECONFIGURE}" == "1" ]] && RECONFIGURE_REASON="KODI_RECONFIGURE=1"
  [[ -f "${CONFIG_STAMP}" ]] || RECONFIGURE_REASON="first build for ${DEPS_DIR}"
  if [[ -n "${TREE_HOST}" && "${TREE_HOST}" != "${HOST_TRIPLE}" ]]; then
    RECONFIGURE_REASON="the tree is configured for ${TREE_HOST}, this build is ${HOST_TRIPLE}"
  fi

  if [[ -n "${RECONFIGURE_REASON}" ]]; then
    log "Configuring depends for ${HOST_TRIPLE} (${RECONFIGURE_REASON})"
    CCACHE_FLAG=--disable-ccache
    [[ "${KODI_ENABLE_CCACHE}" == "1" ]] && CCACHE_FLAG=--enable-ccache
    ./configure \
      --with-tarballs="${TARBALLS}" \
      --host="${HOST_TRIPLE}" \
      --with-sdk-path="${ANDROID_HOME}" \
      --with-ndk-api="${KODI_NDK_API}" \
      --prefix="${PREFIX}" \
      ${DEPENDS_DEBUG_FLAG} \
      ${CCACHE_FLAG}
    mkdir -p "${PREFIX}/${DEPS_DIR}"
    touch "${CONFIG_STAMP}"
  else
    info "depends already configured (KODI_RECONFIGURE=1 to redo)"
  fi

  log "Building depends (this is the long one on a cold volume)"
  make -j"${KODI_JOBS}"
else
  log "Skipping depends (KODI_SKIP_DEPENDS=1)"
fi

[[ -x "${NATIVEPREFIX}/bin/m4" ]] || die "${NATIVEPREFIX}/bin/m4 missing - depends did not finish"

# libtoolize (pulled in by libass) cannot find m4 on its own inside the build.
export M4="${NATIVEPREFIX}/bin/m4"

# ---------------------------------------------------------------------------
# Configure Kodi
#
# Two traps, both silent, both documented in KODI_ANDROID_BUILD_HANDBOOK.md:
#   * Configuration= must be passed or DEBUG_BUILD=yes in depends drags the
#     build type to Debug.
#   * -DAPP_PACKAGE must be passed on every fresh configure or version.txt's
#     org.xbmc.kodi is baked in, and the APK then refuses to install over an
#     existing ${KODI_APP_PACKAGE} - or overwrites stock Kodi.
# Both are asserted against CMakeCache.txt below before anything is compiled.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Stale libbluray guard
#
# FindBluray.cmake runs SEARCH_EXISTING_PACKAGES() before it decides to build:
# an already-installed libbluray of the required version is reused verbatim, and
# neither a source change nor a cmake change makes it reconsider. So a libbluray
# compiled with the WRONG JVM search path survives every subsequent rebuild and
# keeps getting linked in - silently, because nothing about it looks stale.
#
# That is what shipped on armeabi-v7a on 2026-08-12: cpu_family reached meson as
# an empty string, JAVA_ARCH became "", libbluray searched lib//server, and every
# BD-J disc died with "Could not load the java vm". Fixing the cmake did not help
# until the cached library was removed.
#
# Cheap to check, so check it every run and evict rather than trust.
#
# The same staleness applies to EVERY patch under tools/depends/target/libbluray,
# not just the JVM path one. libbluray is NOT in the depends DEPENDS list, so the
# depends Makefile - whose DEPS does list the patch files, and which would there-
# fore rebuild on a patch change - never runs for it. The internal cmake build
# takes over, and that one is gated on the ExternalProject stamp and the installed
# archive, neither of which notices a new patch. So adding a patch and pressing
# Start produces a build that silently does NOT contain it.
#
# 2026-08-13: 007-all-navigation-angle-oob.patch fixes an out-of-bounds read that
# crashes BD-J discs at title transitions. Without the patch-set check below, the
# first clean-room build of that fix is precisely the one that would not have it.
#
# So hash the patch set and evict when it moves. A missing stamp counts as a
# mismatch, which makes existing state volumes rebuild libbluray exactly once.
# ---------------------------------------------------------------------------
if [[ "${KODI_BUNDLE_BDJ}" == "1" ]]; then
  DEPS_PREFIX="${PREFIX}/${DEPS_DIR}"
  CACHED_BLURAY="${DEPS_PREFIX}/lib/libbluray.a"
  BLURAY_PATCH_STAMP="${DEPS_PREFIX}/lib/.libbluray-patchset.sha256"
  BLURAY_PATCHSET="$(cat "${KODI_SRC}"/tools/depends/target/libbluray/*.patch 2>/dev/null \
                     | sha256sum | cut -d' ' -f1)"

  evict_why=""
  if [[ -f "${CACHED_BLURAY}" ]]; then
    if ! grep -aq "lib/${BDJ_JAVA_ARCH}/server" "${CACHED_BLURAY}"; then
      evict_why="its JVM search path is not lib/${BDJ_JAVA_ARCH}"
    elif [[ "$(cat "${BLURAY_PATCH_STAMP}" 2>/dev/null)" != "${BLURAY_PATCHSET}" ]]; then
      evict_why="the libbluray patch set changed since it was built"
    fi
  fi

  if [[ -n "${evict_why}" ]]; then
    log "Evicting cached libbluray - ${evict_why}"
    rm -f  "${DEPS_PREFIX}"/lib/libbluray.a \
           "${DEPS_PREFIX}"/lib/pkgconfig/libbluray.pc \
           "${BLURAY_PATCH_STAMP}"
    rm -rf "${DEPS_PREFIX}"/include/libbluray \
           "${BUILD_DIR}/build/build-libbluray" \
           "${BUILD_DIR}/CMakeFiles/build-libbluray-complete"
    info "it will be re-downloaded, re-patched and rebuilt by this run"
  fi
fi

log "Configuring Kodi build system"
cd "${KODI_SRC}"
make -C tools/depends/target/cmakebuildsys \
     BUILD_DIR="${BUILD_DIR}" \
     Configuration="${KODI_BUILD_CONFIG}" \
     CMAKE_EXTRA_ARGUMENTS="-DAPP_PACKAGE=${KODI_APP_PACKAGE} ${KODI_EXTRA_CMAKE_ARGS}"

CACHE="${BUILD_DIR}/CMakeCache.txt"
[[ -f "${CACHE}" ]] || die "${CACHE} was not generated"
grep -q "^APP_PACKAGE:UNINITIALIZED=${KODI_APP_PACKAGE}$" "${CACHE}" \
  || die "APP_PACKAGE did not reach CMakeCache.txt - the APK would be built as org.xbmc.kodi"
grep -q "^CMAKE_BUILD_TYPE:STRING=${KODI_BUILD_CONFIG}$" "${CACHE}" \
  || die "CMAKE_BUILD_TYPE is not ${KODI_BUILD_CONFIG} - $(grep '^CMAKE_BUILD_TYPE' "${CACHE}")"
info "CMakeCache.txt verified: APP_PACKAGE and CMAKE_BUILD_TYPE are as requested"

# ---------------------------------------------------------------------------
# Build + package
# ---------------------------------------------------------------------------
log "Building libkodi.so"
make -C "${BUILD_DIR}" -j"${KODI_JOBS}"

# Record which libbluray patch set the now-installed archive was built from, so
# the guard above can evict it when a patch is added, changed or removed. Written
# only after a successful build, so a failed run cannot stamp a stale archive.
if [[ "${KODI_BUNDLE_BDJ}" == "1" && -f "${CACHED_BLURAY}" ]]; then
  printf '%s\n' "${BLURAY_PATCHSET}" > "${BLURAY_PATCH_STAMP}"
fi

# ---------------------------------------------------------------------------
# BD-J jars
#
# The jars in the JRE image were built in the Dockerfile from a PRISTINE
# libbluray tarball, before this source tree existed - so any patch under
# tools/depends/target/libbluray that touches bdj/java/** is not in them.  The
# libbluray the build just compiled has those patches applied (FindBluray.cmake
# lists them), so recompile the affected classes from that tree and splice them
# into our copy of the jars.  Same class of trap as the cached libbluray.a
# above: without this, adding a BD-J patch and pressing Start produces an APK
# that silently does not contain it.
# ---------------------------------------------------------------------------
BDJ_REBUILD="${KODI_SRC}/tools/android/packaging/jre/rebuild-bdj-jars.sh"
if [[ "${KODI_BUNDLE_BDJ}" == "1" && ! -x "${BDJ_REBUILD}" ]]; then
  # Source revisions older than the script simply have no BD-J patches to apply,
  # so this image must still build them rather than failing.
  info "no rebuild-bdj-jars.sh in this source tree - BD-J jars left as the image built them"
elif [[ "${KODI_BUNDLE_BDJ}" == "1" ]]; then
  log "Rebuilding BD-J jars from the patched libbluray source"
  if [[ ! -x "${BDJ_JDK8_HOME:-}/bin/javac" ]]; then
    die "BDJ_JDK8_HOME is not a JDK 8 (${BDJ_JDK8_HOME:-unset}) - rebuild the image"
  fi
  JAVA8_HOME="${BDJ_JDK8_HOME}" \
  JRE_IMAGE_DIR="${JRE_IMAGE_DIR}" \
  PATCH_DIR="${KODI_SRC}/tools/depends/target/libbluray" \
    "${BDJ_REBUILD}" \
      "${BUILD_DIR}/build" \
    || die "BD-J jar rebuild failed - the APK would ship unpatched BD-J classes"
fi

log "Packaging and signing the APK"
# Gradle's file-watcher is useless in a throwaway container and breaks on some
# bind mounts.
export GRADLE_OPTS="${GRADLE_OPTS:--Dorg.gradle.vfs.watch=false}"
make -C "${BUILD_DIR}" apk

APK_NAME="$(awk '/APP_NAME/ {print tolower($2)}' "${KODI_SRC}/version.txt")app-${KODI_ARCH}-$(echo "${KODI_BUILD_CONFIG}" | tr 'A-Z' 'a-z').apk"
APK="${KODI_SRC}/${APK_NAME}"
[[ -f "${APK}" ]] || die "expected APK ${APK} was not produced"

# ---------------------------------------------------------------------------
# Verify
#
# These checks are cheap and each of them has caught a real, shipped mistake.
# They are NOT a substitute for launching the APK on a device.
# ---------------------------------------------------------------------------
log "Verifying ${APK_NAME}"
AAPT2="$(ls -1 "${ANDROID_HOME}"/build-tools/*/aapt2 | tail -1)"

PKG="$("${AAPT2}" dump packagename "${APK}")"
info "package name   : ${PKG}"
[[ "${PKG}" == "${KODI_APP_PACKAGE}" ]] \
  || die "APK package is ${PKG}, expected ${KODI_APP_PACKAGE}"

OWNER="$(keytool -printcert -jarfile "${APK}" | awk -F'Owner: ' '/Owner:/ {print $2; exit}')"
info "signed by      : ${OWNER}"
if [[ "${KODI_BUILD_CONFIG}" == "Release" && "${KODI_ALLOW_DEBUG_KEY}" != "1" ]]; then
  [[ "${OWNER}" != *"CN=Android Debug"* ]] || die "Release APK is signed with a debug key"
fi

if [[ "${KODI_BUNDLE_BDJ}" == "1" ]]; then
  JVM_COUNT="$(unzip -l "${APK}" | grep -c 'assets/j2re-image/.*libjvm\.so' || true)"
  JAR_COUNT="$(unzip -l "${APK}" | grep -c 'assets/j2re-image/libbluray-j2se-.*\.jar' || true)"

  # Prove the shipped jars really carry our BD-J patches rather than the stock
  # classes the image built.  Comparing entry CRCs against the image's pristine
  # jars is the honest test: the class NAME is present either way, so grepping
  # for it would pass on an unpatched APK - which is the exact failure this
  # whole rebuild step exists to prevent.
  BDJ_PATCHED_CLASSES="$(grep -h '^+++ b/' "${KODI_SRC}"/tools/depends/target/libbluray/*.patch 2>/dev/null \
    | sed 's|^+++ b/||;s|[[:space:]].*$||' \
    | grep -E '^src/libbluray/bdj/java.*\.java$' \
    | sed 's|^src/libbluray/bdj/java[^/]*/||;s|\.java$|.class|' | sort -u || true)"
  if [[ -n "${BDJ_PATCHED_CLASSES}" ]]; then
    BDJ_TMP="$(mktemp -d)"
    unzip -qo "${APK}" 'assets/j2re-image/libbluray-*j2se-*.jar' -d "${BDJ_TMP}"
    crc_of() { unzip -v "$1" 2>/dev/null | awk -v e="$2" '$NF==e {print $7; exit}'; }
    for cls in ${BDJ_PATCHED_CLASSES}; do
      for jar in "${BDJ_TMP}"/assets/j2re-image/libbluray-*j2se-*.jar; do
        stock="/opt/bdj-jars/$(basename "${jar}")"
        [[ -f "${stock}" ]] || continue
        have="$(crc_of "${jar}"   "${cls}")"
        was="$(crc_of  "${stock}" "${cls}")"
        [[ -n "${have}" && -n "${was}" ]] || continue
        [[ "${have}" != "${was}" ]] \
          || die "${cls} in the packaged jars is byte-identical to the unpatched one - the BD-J jar rebuild did not take"
        info "BD-J patched   : ${cls} (crc ${was} -> ${have})"
      done
    done
    rm -rf "${BDJ_TMP}"
  fi

  [[ "${JVM_COUNT}" -ge 1 ]] || die "no libjvm.so in assets/j2re-image - BD-J menus will not work"
  [[ "${JAR_COUNT}" -ge 1 ]] || die "no libbluray-j2se jar in assets/j2re-image - BD-J menus will not work"
  info "BD-J payload   : libjvm.so + BD-J jars present"

  # Shipping both halves is not the same as them agreeing. libbluray dlopen()s
  # $JAVA_HOME/<one of these>/libjvm.so, and the <arch> component is frozen at
  # compile time - so a JRE that is present but laid out under a different
  # directory name is indistinguishable, from Kodi's side, from no JRE at all.
  # Match the paths in the SHIPPED binary against the SHIPPED JRE; anything less
  # (checking the source, the cmake, or the arch name) has already been fooled.
  BDJ_TMP="$(mktemp -d)"
  unzip -o -q "${APK}" "lib/${KODI_ARCH}/libkodi.so" -d "${BDJ_TMP}"
  # `lib/*/server` with an empty middle component is the bug's signature, so the
  # glob must be allowed to match nothing - do not "tighten" [a-z0-9_]* to +.
  JVM_SEARCH="$(grep -ao 'lib/[a-z0-9_]*/\(server\|client\)' \
                     "${BDJ_TMP}/lib/${KODI_ARCH}/libkodi.so" | sort -u || true)"
  BDJ_PATH_OK=0
  while read -r p; do
    [[ -n "${p}" ]] || continue
    if [[ -f "${JRE_IMAGE_DIR}/${p}/libjvm.so" ]]; then
      BDJ_PATH_OK=1
      info "BD-J JVM path  : ${p} (resolves in the bundled JRE)"
    fi
  done <<< "${JVM_SEARCH}"
  rm -rf "${BDJ_TMP}"

  if [[ "${BDJ_PATH_OK}" != "1" ]]; then
    info "libkodi.so searches for libjvm.so in:"
    info "${JVM_SEARCH:-<no search paths found at all - is libbluray linked in?>}"
    info "the bundled JRE has libjvm.so in:"
    info "$(cd "${JRE_IMAGE_DIR}" && find . -name libjvm.so -printf '%h\n' | sed 's|^\./||')"
    die "the JVM search path compiled into libbluray does not exist in the bundled JRE -
       every BD-J disc would fail with 'Could not load the java vm'. Expected
       lib/${BDJ_JAVA_ARCH}/...; an EMPTY arch component means cpu_family did not reach
       the meson cross file (cmake/scripts/common/ModuleHelpers.cmake), a WRONG one means
       005-aarch64-java-arch.patch was not applied (cmake/modules/FindBluray.cmake)"
  fi
fi

# Staleness canary: this fork's disc-subtitle slider. The data files in the APK
# come from the depends PREFIX, not from build/, so they can lag the source.
# grep -c rather than grep -q: -q closes the pipe early and, under pipefail,
# SIGPIPEs unzip into a false negative.
SETTINGS_HITS="$(unzip -p "${APK}" assets/system/settings/settings.xml 2>/dev/null | grep -c subtitlepeakluminance || true)"
if [[ "${SETTINGS_HITS}" -ge 1 ]]; then
  info "settings.xml   : this fork's disc-menu settings are present"
else
  info "settings.xml   : WARNING - disc.subtitlepeakluminance missing; the data files in this"
  info "                 APK may be older than the source (stale depends prefix)"
fi

# ---------------------------------------------------------------------------
# Deliver
# ---------------------------------------------------------------------------
STAMP="$(date +%Y%m%d-%H%M)"
OUT="${KODI_OUTPUT}/${APK_NAME%.apk}-${SRC_DESC}-${STAMP}.apk"
cp -p "${APK}" "${OUT}"
ln -sf "$(basename "${OUT}")" "${KODI_OUTPUT}/${APK_NAME}"

log "Done"
info "$(du -h "${OUT}" | cut -f1)  ${OUT}"
info ""
info "Install it and LAUNCH it before trusting it. A package-name and signature"
info "check does not catch an abort in JNI_OnLoad or a disc that will not play."
