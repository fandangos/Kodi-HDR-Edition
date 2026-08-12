#!/usr/bin/env bash
#
# Ship the built images to the Unraid box over the LAN.
#
#   ./deploy-to-unraid.sh <unraid-host> [--registry] [--key <keystore>] [tag ...]
#
#   --registry  run a registry on the Unraid box and push into it, then install
#               the Unraid templates. Do this if you want to drive the builds
#               from the web UI.
#   --key       also copy a signing keystore (and its .pass file, if present)
#               into each container's keys/ directory.
#
# Needs ssh to root@<unraid-host>. Sends about 0.5 GB for both slim tags.
#
# Why this exists: a registry round-trip sends the image up a domestic uplink and
# back down again for no reason when both machines are on the same switch.
#
# Every ssh, scp and the registry port forward share ONE connection, so a box
# without key auth asks for the password once rather than a dozen times.

set -euo pipefail

HOST="${1:-}"
shift || true

KEYSTORE=""
KEY_DEST="${KEY_DEST:-/mnt/cache/appdata}"
USE_REGISTRY=0

while [[ "${1:-}" == --* ]]; do
  case "$1" in
    --key)
      KEYSTORE="${2:?--key needs a path to a keystore}"
      shift 2
      ;;
    --registry)
      # Run a registry ON the Unraid box and push into it, instead of
      # docker save | docker load. Slower by a few seconds, but it is what makes
      # the Unraid web UI usable: "Add Container" always runs a docker pull, so a
      # template pointing at an image that exists only in the local image store
      # fails at Apply. A registry at localhost:5000 pulls like any other.
      USE_REGISTRY=1
      shift
      ;;
    *)
      echo "unknown option: $1" >&2; exit 1
      ;;
  esac
done

TAGS=("$@")
[[ ${#TAGS[@]} -gt 0 ]] || TAGS=(arm64-v8a armeabi-v7a)
IMAGE="${KODI_BUILDER_IMAGE:-fandangos/kodi-android-builder}"

[[ -n "${HOST}" ]] || {
  echo "usage: $0 <unraid-host> [--registry] [--key <keystore>] [tag ...]" >&2; exit 1; }

# --- one ssh connection for the whole run ----------------------------------
SSH_CTRL="$(mktemp -u /tmp/kodi-deploy-ssh.XXXXXX)"
FORWARDED=0

cleanup() {
  if [[ -S "${SSH_CTRL}" ]]; then
    [[ "${FORWARDED}" == "1" ]] && \
      ssh -S "${SSH_CTRL}" -O cancel -L "${LOCAL_PORT:-5000}":localhost:5000 "root@${HOST}" 2>/dev/null || true
    ssh -S "${SSH_CTRL}" -O exit "root@${HOST}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

echo "==> connecting to ${HOST}"
ssh -f -N -M -S "${SSH_CTRL}" -o ControlPersist=yes "root@${HOST}" \
  || { echo "could not open an ssh connection to root@${HOST}" >&2; exit 1; }

rsh()  { ssh -S "${SSH_CTRL}" "root@${HOST}" "$@"; }
rcp()  { scp -q -o "ControlPath=${SSH_CTRL}" "$@"; }

USE_ZSTD=1
if [[ "${KODI_NO_ZSTD:-0}" == "1" ]]; then
  USE_ZSTD=0
elif ! command -v zstd >/dev/null || ! rsh 'command -v zstd >/dev/null'; then
  echo "zstd missing on one end - sending uncompressed (about twice the bytes, still LAN speed)"
  USE_ZSTD=0
fi

REFS=()
for tag in "${TAGS[@]}"; do
  docker image inspect "${IMAGE}:${tag}" >/dev/null \
    || { echo "no local image ${IMAGE}:${tag} - build it first (see publish.sh or README)" >&2; exit 1; }
  REFS+=("${IMAGE}:${tag}")

  # This script SHIPS images, it does not build them, and a stale one ships
  # perfectly happily: the bytes arrive, the registry accepts them, Unraid
  # correctly reports no update, and the change you came here to deploy is
  # simply not in it. The entrypoint is COPYed into the image, so editing it
  # without rebuilding is exactly the case that looks like a deploy problem
  # and is not one.
  IMG_EPOCH="$(docker image inspect -f '{{.Created}}' "${IMAGE}:${tag}" | xargs -I{} date -d {} +%s 2>/dev/null || echo 0)"
  for f in build-kodi-android.sh Dockerfile; do
    SRC_EPOCH="$(stat -c %Y "$(dirname "$0")/${f}" 2>/dev/null || echo 0)"
    if [[ "${SRC_EPOCH}" -gt "${IMG_EPOCH}" && "${IMG_EPOCH}" != "0" ]]; then
      echo
      echo "WARNING: ${f} is newer than the image ${IMAGE}:${tag}." >&2
      echo "         Shipping it will deploy the OLD ${f}. Rebuild first:" >&2
      echo "           docker build --build-arg KODI_ARCH=${tag} --build-arg BAKE_ANDROID_SDK=0 \\" >&2
      echo "                        -t ${IMAGE}:${tag} $(dirname "$0")" >&2
      echo
      [[ "${KODI_DEPLOY_STALE_OK:-0}" == "1" ]] \
        || { echo "set KODI_DEPLOY_STALE_OK=1 to ship it anyway" >&2; exit 1; }
    fi
  done
done

# --- the images -------------------------------------------------------------
if [[ "${USE_REGISTRY}" == "1" ]]; then
  REG_DATA="${REG_DATA:-/mnt/cache/appdata/registry}"
  LOCAL_PORT="${LOCAL_PORT:-5000}"

  echo "==> ensuring a registry is running on ${HOST}"
  rsh "
    docker inspect kodi-registry >/dev/null 2>&1 || \
      docker run -d --name kodi-registry --restart=always \
        -p 5000:5000 -v '${REG_DATA}':/var/lib/registry registry:2 >/dev/null
    docker start kodi-registry >/dev/null 2>&1 || true"

  # Forward through the existing connection so the daemon here sees "localhost",
  # which Docker treats as an insecure registry by default. Pushing to
  # <host>:5000 directly would need insecure-registries in the daemon config on
  # THIS machine, i.e. a nixos-rebuild, for no benefit. The host:port is only an
  # endpoint - the repository name stored in the registry is the part after it,
  # so Unraid pulls the same image as localhost:5000/... whatever port is used here.
  ssh -S "${SSH_CTRL}" -O forward -L "${LOCAL_PORT}":localhost:5000 "root@${HOST}" \
    || { echo "could not forward localhost:${LOCAL_PORT} - set LOCAL_PORT to a free port" >&2; exit 1; }
  FORWARDED=1

  for tag in "${TAGS[@]}"; do
    echo "==> pushing ${tag}"
    docker tag "${IMAGE}:${tag}" "localhost:${LOCAL_PORT}/kodi-android-builder:${tag}"
    docker push "localhost:${LOCAL_PORT}/kodi-android-builder:${tag}"
  done

  echo "==> installing Unraid templates"
  rsh 'mkdir -p /boot/config/plugins/dockerMan/templates-user'
  rcp "$(dirname "$0")"/unraid/my-kodi-build-*.xml \
      "root@${HOST}:/boot/config/plugins/dockerMan/templates-user/"
else
  # One docker save for every tag, not one per tag: the tags share all but a few
  # kB of layers, and a single stream sends those layers once.
  echo "==> ${REFS[*]} -> ${HOST}"
  if [[ "${USE_ZSTD}" == "1" ]]; then
    # -1 rather than a higher level on purpose: on a 2.5GbE link the CPU is the
    # bottleneck, not the wire, and level 1 still takes a useful bite out of it.
    docker save "${REFS[@]}" | zstd -1 -T0 | rsh 'zstd -d | docker load'
  else
    docker save "${REFS[@]}" | rsh 'docker load'
  fi
fi

# --- the signing key --------------------------------------------------------
if [[ -n "${KEYSTORE}" ]]; then
  [[ -r "${KEYSTORE}" ]] || { echo "cannot read ${KEYSTORE}" >&2; exit 1; }
  echo "==> signing key -> ${HOST}"

  # Sanity-check the pair here, where the human is, rather than letting the
  # build fail on the far side. A .pass file written with `echo` has a trailing
  # newline; the build strips it, so accept it either way.
  PASSFILE="${KEYSTORE}.pass"
  if [[ -r "${PASSFILE}" ]]; then
    if keytool -list -keystore "${KEYSTORE}" \
         -storepass "$(tr -d '\r\n' < "${PASSFILE}")" >/dev/null 2>&1; then
      echo "    keystore and ${PASSFILE##*/} verified"
    else
      echo "    WARNING: ${PASSFILE##*/} does not open this keystore - copying anyway" >&2
    fi
  else
    echo "    no ${PASSFILE##*/} next to the keystore - you will have to supply"
    echo "    KODI_ANDROID_STORE_PASSWORD yourself when creating the containers"
  fi

  # The build runs as PUID:PGID, so the key has to be owned by it. Copied in as
  # root with mode 600 it would be unreadable to the build, which is a failure
  # that only shows up after the SDK download.
  BUILD_UID="${BUILD_UID:-99}"
  BUILD_GID="${BUILD_GID:-100}"

  for bits in 64 32; do
    ROOT_DIR="${KEY_DEST}/kodi-android-builder${bits}"
    D="${ROOT_DIR}/keys"
    rsh "mkdir -p '${D}'"
    if [[ -r "${PASSFILE}" ]]; then
      rcp "${KEYSTORE}" "${PASSFILE}" "root@${HOST}:${D}/"
    else
      rcp "${KEYSTORE}" "root@${HOST}:${D}/"
    fi
    # The data directory itself too: the container creates src/, state/ and
    # release/ inside it as root on first run, but a directory Unraid or an
    # earlier copy left owned by root would stop the build user writing there.
    rsh "chown -R ${BUILD_UID}:${BUILD_GID} '${ROOT_DIR}' && chmod 700 '${D}' && chmod 600 '${D}'/*"
    echo "    -> ${D}/$(basename "${KEYSTORE}")  (owner ${BUILD_UID}:${BUILD_GID})"
  done
fi

# --- what to do next --------------------------------------------------------
echo
if [[ "${USE_REGISTRY}" == "1" ]]; then
cat <<EOF
Done. Everything is in place for the web UI:

  Docker -> Add Container -> Template -> kodi-build-arm64-v8a  (under "User templates")

Check the data directory path and Apply. Repeat for kodi-build-armeabi-v7a. After that
a build is the Start button, and the settings are editable like any other container.

Leave the "Keystore password" field EMPTY - the password is read from the .pass file
next to the keystore, which keeps it out of docker inspect and out of the template on
the flash drive.

Re-running this script later updates the images in place. To pick a new image up, click
the container -> Edit -> Apply, which recreates it against the pulled image. /data is a
bind mount, so src/, state/ and release/ survive untouched.

Start alone will NOT do it: the container was created from the old image ID and docker
start reuses it. Neither will "Check for Updates" - that check cannot read a private
localhost:5000 registry and reports "not available" with a broken-image icon. Expected
for this setup, not a fault.
EOF
else
cat <<EOF
Done. The images are in ${HOST}'s local image store, which the web UI cannot create a
container from - "Add Container" runs a docker pull that will fail for them. Create the
containers once from the Unraid terminal; they appear in the Docker tab afterwards with
working Start/Stop/Logs:

  for abi in arm64-v8a armeabi-v7a; do
    [ "\$abi" = arm64-v8a ] && bits=64 || bits=32
    docker create --name kodi-build-\${abi} \\
      -v /mnt/cache/appdata/kodi-android-builder\${bits}:/data \\
      -e KODI_DATA=/data \\
      -e KODI_ARCH=\${abi} \\
      -e KODI_APP_PACKAGE=org.xbmc.fandangos \\
      -e KODI_BUILD_CONFIG=Release \\
      -e KODI_GIT_URL=https://github.com/fandangos/Kodi-HDR-Edition.git \\
      -e KODI_GIT_REF=android-bluray-disc-menus \\
      -e KODI_GIT_PULL=1 \\
      -e KODI_ANDROID_STORE_FILE=/data/keys/fandangos-release.keystore \\
      -e KODI_ANDROID_KEY_ALIAS=fandangos \\
      --restart=no \\
      ${IMAGE}:\${abi}
  done

Run --registry instead if you would rather drive this from the web UI.
EOF
fi

cat <<EOF

Two containers, one per ABI: separate Start buttons, separate logs, and one can fail
without blocking the other. Separate data directories also let them run at the same
time, at the cost of a second 2.7 GB SDK download plus another copy of the tarballs
and Gradle home - roughly 4 GB.

  * Budget about 22 GB per ABI: ~14 GB of source tree once depends has built inside
    it, ~8 GB of state.
  * The first build of each is ~1 hour and needs dl.google.com. Later ones are
    incremental, 5-15 minutes.
  * To keep the APKs on the array instead of the cache, add a second mount and point
    KODI_OUTPUT at it.
EOF
