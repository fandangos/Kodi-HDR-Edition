#!/usr/bin/env bash
#
# Build and push both ABI tags to a registry.
#
#   ./publish.sh <namespace> [extra-version-tag]
#
# Registry is Docker Hub by default. For GitHub's registry:
#
#   REGISTRY=ghcr.io ./publish.sh <github-user>
#
# `docker login` (or `docker login ghcr.io` with a PAT that has write:packages)
# first.
#
# On a slow uplink consider tools/docker/android-builder/deploy-to-unraid.sh
# instead - it copies the image over the LAN and skips the registry entirely.

set -euo pipefail

NAMESPACE="${1:-}"
VERSION="${2:-}"
REGISTRY="${REGISTRY:-}"
IMAGE="${REGISTRY:+${REGISTRY}/}${NAMESPACE}/kodi-android-builder"

[[ -n "${NAMESPACE}" ]] || { echo "usage: [REGISTRY=ghcr.io] $0 <namespace> [version-tag]" >&2; exit 1; }

cd "$(dirname "$0")"

# The PUBLISHED images are slim: they fetch the pinned SDK/NDK from Google on
# first run instead of carrying a 1 GB layer through your uplink. Everything
# else - JRE, BD-J jars, toolchain, entrypoint - is still baked in, and the
# component versions are pinned identically, so the build is the same build.
# PUSH_FULL=1 also pushes the self-contained images as :<abi>-full.
for arch in arm64-v8a armeabi-v7a; do
  echo "==> building ${IMAGE}:${arch} (slim)"
  docker build --build-arg "KODI_ARCH=${arch}" --build-arg BAKE_ANDROID_SDK=0 \
               -t "${IMAGE}:${arch}" .
  [[ -n "${VERSION}" ]] && docker tag "${IMAGE}:${arch}" "${IMAGE}:${arch}-${VERSION}"

  if [[ "${PUSH_FULL:-0}" == "1" ]]; then
    echo "==> building ${IMAGE}:${arch}-full (SDK baked in)"
    docker build --build-arg "KODI_ARCH=${arch}" --build-arg BAKE_ANDROID_SDK=1 \
                 -t "${IMAGE}:${arch}-full" .
  fi
done

# `latest` is the 64-bit image: it is the one that is actually tested on hardware.
docker tag "${IMAGE}:arm64-v8a" "${IMAGE}:latest"

# One layer is ~1 GB (the NDK). Docker's default of 5 parallel uploads splits a
# limited uplink between them, and Docker Hub expires an upload session that
# takes too long - which shows up as "blob upload invalid - upload state
# expired" after an hour of progress. Pushing the big tag on its own first, and
# retrying, is what makes this survive a domestic connection.
push_with_retry() {
  local ref="$1" attempt
  for attempt in 1 2 3 4 5; do
    if docker push "${ref}"; then
      return 0
    fi
    echo "!!! push of ${ref} failed (attempt ${attempt}) - retrying; layers that completed are kept"
    sleep 10
  done
  echo "push of ${ref} failed 5 times. If it is always the same large layer, the registry is" >&2
  echo "expiring the upload: see deploy-to-unraid.sh for the no-registry route." >&2
  return 1
}

for arch in arm64-v8a armeabi-v7a; do
  push_with_retry "${IMAGE}:${arch}"
  [[ -n "${VERSION}" ]] && push_with_retry "${IMAGE}:${arch}-${VERSION}"
  [[ "${PUSH_FULL:-0}" == "1" ]] && push_with_retry "${IMAGE}:${arch}-full"
done
push_with_retry "${IMAGE}:latest"

echo
echo "Pushed:"
echo "  ${IMAGE}:arm64-v8a"
echo "  ${IMAGE}:armeabi-v7a"
echo "  ${IMAGE}:latest  (= arm64-v8a)"
[[ -n "${VERSION}" ]] && echo "  ...and the -${VERSION} tags"
echo
echo "If this is a new registry, update <Repository> in unraid/*.xml to ${IMAGE}."
