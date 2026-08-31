# syntax=docker/dockerfile:1
#
# Kodi Android cross-build container (arm64-v8a / armeabi-v7a).
#
# The image carries everything that is slow or awkward to obtain: the Android
# SDK/NDK, JDK 17 for Gradle, the BD-J JRE images for both ABIs, and the BD-J
# jars built from the same libbluray release the depends system compiles.
# The Kodi source, the build state and the signing key are supplied at run time.
#
# Build both tags (they share every layer except the default ABI):
#   docker build -t <user>/kodi-android-builder:arm64-v8a   --build-arg KODI_ARCH=arm64-v8a   .
#   docker build -t <user>/kodi-android-builder:armeabi-v7a --build-arg KODI_ARCH=armeabi-v7a .
#
# See README.md.

# ---------------------------------------------------------------------------
# Stage 1 - BD-J jars.
#
# libbluray's BD-J classes are arch independent, so one build serves both ABIs.
# They are NOT part of the JRE images published as the script.service.jre addon
# (those ship a copy of the 2022 1.3.2 jars, renamed in the aarch64 release);
# libbluray 1.5.0 looks for libbluray-j2se-1.5.0.jar by exact name, so the jars
# are built here from the same tarball tools/depends/target/libbluray consumes.
#
# JDK 8 is used deliberately: libbluray's own meson logic (src/libbluray/bdj/
# meson.build) selects source/target 1.4 + 1.5 for javac < 9, which is what the
# hand-built jars in the known-good arm64 APK were compiled with. The bundled
# JRE is Java 8, so nothing newer may be targeted.
# ---------------------------------------------------------------------------
FROM eclipse-temurin:8-jdk AS bdj-jars

ARG LIBBLURAY_VERSION=1.5.0
ARG LIBBLURAY_SHA512=32725c8490479b134f2fc9ec72e77a6f57a85b3de33a4a3ad2c82575b7045c0d31a997c0dd05f1dc175628cd2ce61f58573f58a7b799057ec4d6984616b0e732
ARG LIBBLURAY_URL=https://mirrors.kodi.tv/build-deps/sources

RUN apt-get update && apt-get install -y --no-install-recommends \
      ant bzip2 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
RUN curl -LsS --retry 10 --retry-connrefused \
      -o libbluray.tar.bz2 "${LIBBLURAY_URL}/libbluray-${LIBBLURAY_VERSION}.tar.bz2" \
    && echo "${LIBBLURAY_SHA512}  libbluray.tar.bz2" | sha512sum -c - \
    && mkdir -p src && tar --strip-components=1 -xf libbluray.tar.bz2 -C src

# Mirrors the ant invocation in src/libbluray/bdj/meson.build. Output names must
# stay libbluray-j2se-<ver>.jar / libbluray-awt-j2se-<ver>.jar - the names are
# compiled into libbluray's BDJ_CLASSPATH.
RUN cd src/src/libbluray/bdj \
    && ant -f build.xml \
        -Dbuild=/work/bdj-build \
        -Ddist=/opt/bdj-jars \
        -Dbootclasspath= \
        -Dsrc_awt=:java-j2se:java-build-support \
        -Djavac_path="${JAVA_HOME}/bin/javac" \
        -Djavac_arg=-Xlint:-deprecation \
        -Dversion="j2se-${LIBBLURAY_VERSION}" \
        -Djava_version_asm=1.5 \
        -Djava_version_bdj=1.4 \
    && ls -l /opt/bdj-jars \
    && test -s "/opt/bdj-jars/libbluray-j2se-${LIBBLURAY_VERSION}.jar" \
    && test -s "/opt/bdj-jars/libbluray-awt-j2se-${LIBBLURAY_VERSION}.jar"

# ---------------------------------------------------------------------------
# Stage 2 - the builder image.
#
# Ubuntu 24.04 is pinned on purpose: it ships autoconf 2.71.
# `tools/depends/bootstrap` regenerates configure with the HOST autoconf, and
# from 2.72 AC_PROG_CC appends the detected C standard flag to CC itself. $CC
# then reads "<path>/aarch64-linux-android24-clang -std=gnu23" and configure.ac
# writes it verbatim into the meson cross-file, which tries to exec a path with
# a space in it. That is the whole first half of KODI_ANDROID_BUILD_HANDBOOK.md,
# and it is a property of the build host rather than of Kodi. Do not casually
# bump this base image.
# ---------------------------------------------------------------------------
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Package list from docs/README.Android.md plus what depends' native tools need.
# The lib32* entries in that guide are for pre-r28 NDKs and are deliberately
# omitted - NDK r28 has no 32-bit host binaries.
RUN apt-get update && apt-get install -y --no-install-recommends \
      autoconf \
      automake \
      bison \
      bzip2 \
      build-essential \
      ca-certificates \
      ccache \
      curl \
      file \
      flex \
      gawk \
      git \
      gperf \
      libcurl4-openssl-dev \
      libtool \
      openjdk-17-jdk-headless \
      patch \
      perl \
      pkg-config \
      python3 \
      python3-venv \
      rsync \
      unzip \
      util-linux \
      xz-utils \
      zip \
      zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

ENV JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=/opt/android-sdk

ARG ANDROID_CMDLINE_TOOLS=13114758
ARG ANDROID_NDK_VERSION=28.2.13676358
ARG ANDROID_PLATFORM=android-37.0
ARG ANDROID_BUILD_TOOLS=37.0.0
# The Android Gradle Plugin pins its own build-tools revision, independent of
# Kodi's compileSdk. AGP 9.2.0 (tools/android/packaging/build.gradle) wants
# 36.0.0 and will try to download it mid-build if it is absent - which fails in
# a container, because the SDK is root-owned and the build runs unprivileged
# ("Failed to install the following SDK components ... The SDK directory is not
# writable"). Ship it. Bump this when the AGP version in build.gradle changes.
ARG ANDROID_AGP_BUILD_TOOLS=36.0.0

# cmdline-tools lives OUTSIDE $ANDROID_HOME and every sdkmanager call passes
# --sdk_root. That is what lets the slim variant put the SDK on the persistent
# volume while $ANDROID_HOME stays /opt/android-sdk in both variants - see
# BAKE_ANDROID_SDK below.
RUN mkdir -p /opt/cmdline-tools \
    && curl -LsS --retry 10 --retry-connrefused -o /tmp/cmdline-tools.zip \
       "https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_CMDLINE_TOOLS}_latest.zip" \
    && unzip -q /tmp/cmdline-tools.zip -d /tmp/cmdline-tools \
    && mv /tmp/cmdline-tools/cmdline-tools /opt/cmdline-tools/latest \
    && rm -rf /tmp/cmdline-tools.zip /tmp/cmdline-tools

ENV PATH=/opt/cmdline-tools/latest/bin:${ANDROID_HOME}/platform-tools:${PATH}
ENV ANDROID_PLATFORM=${ANDROID_PLATFORM} \
    ANDROID_BUILD_TOOLS=${ANDROID_BUILD_TOOLS} \
    ANDROID_AGP_BUILD_TOOLS=${ANDROID_AGP_BUILD_TOOLS}

# 1 = SDK and NDK baked into the image: self-contained, builds offline, ~1.5 GB
#     to ship.
# 0 = "slim": the entrypoint installs the exact same pinned components into
#     /state/android-sdk on first run and symlinks /opt/android-sdk at it, so
#     every path downstream is identical. ~3 GB is then fetched from Google once
#     per state volume instead of being pushed through your uplink.
ARG BAKE_ANDROID_SDK=1

RUN if [ "${BAKE_ANDROID_SDK}" = "1" ]; then \
      set -eux; \
      yes | sdkmanager --sdk_root="${ANDROID_HOME}" --licenses > /dev/null; \
      sdkmanager --sdk_root="${ANDROID_HOME}" --install \
         "platform-tools" \
         "platforms;${ANDROID_PLATFORM}" \
         "build-tools;${ANDROID_BUILD_TOOLS}" \
         "build-tools;${ANDROID_AGP_BUILD_TOOLS}" \
         "ndk;${ANDROID_NDK_VERSION}"; \
      rm -rf "${ANDROID_HOME}/.temp" /root/.android/cache; \
      # The build runs as PUID:PGID, not root, and Gradle insists on writing to
      # the SDK (install properties, licenses). Done in this layer on purpose: a
      # separate chmod layer would duplicate the whole ~2.7 GB tree in the image.
      chmod -R a+rwX "${ANDROID_HOME}"; \
    else \
      echo "slim image: SDK/NDK will be installed into /state on first run"; \
    fi

# --- BD-J JRE images -------------------------------------------------------
# One tree per ABI, staged into the APK by JRE_IMAGE_DIR at package time.
# The 64-bit image lays libjvm.so out under lib/aarch64 (which is why depends
# carries 005-aarch64-java-arch.patch); the 32-bit image uses lib/arm, which is
# libbluray's stock mapping, so that patch is a no-op there rather than a
# problem - it only rewrites the aarch64 branch of the meson conditional.
ARG JRE_ARM64_URL=https://github.com/fandangos/JRE-Kodi-Android/releases/download/1.2-aarch64/script.service.jre-aarch64-v1.2.zip
ARG JRE_ARM32_URL=https://github.com/fandangos/JRE-Kodi-Android/releases/download/1.2/script.service.jre-arm-v1.2.zip
ARG LIBBLURAY_VERSION=1.5.0

COPY --from=bdj-jars /opt/bdj-jars /opt/bdj-jars

# The JDK 8 that built those jars is kept so the runtime build can recompile the
# BD-J classes our libbluray patches touch.  Without it a patch under
# tools/depends/target/libbluray/ that changes bdj/java/** would be silently
# inert in the APK: the jars above are built here from a pristine tarball, long
# before the Kodi source (and therefore the patches) exists.  See
# tools/android/packaging/jre/rebuild-bdj-jars.sh.  JDK 17 cannot stand in - the
# BD-J classes override java.* packages, so --release is unusable, and the
# bundled JRE is Java 8.
COPY --from=bdj-jars /opt/java/openjdk /opt/jdk8
ENV BDJ_JDK8_HOME=/opt/jdk8

RUN set -eux; \
    for spec in "arm64-v8a=${JRE_ARM64_URL}=aarch64" "armeabi-v7a=${JRE_ARM32_URL}=arm"; do \
      abi="${spec%%=*}"; rest="${spec#*=}"; url="${rest%=*}"; jvmdir="${rest##*=}"; \
      curl -LsS --retry 10 --retry-connrefused -o /tmp/addon.zip "${url}"; \
      unzip -q /tmp/addon.zip -d /tmp/addon; \
      mkdir -p "/opt/kodi-jre/${abi}"; \
      unzip -q /tmp/addon/script.service.jre/resources/lib/j2re-image.zip -d "/opt/kodi-jre/${abi}"; \
      test -f "/opt/kodi-jre/${abi}/j2re-image/lib/${jvmdir}/server/libjvm.so" \
        || test -f "/opt/kodi-jre/${abi}/j2re-image/lib/${jvmdir}/client/libjvm.so"; \
      rm -f "/opt/kodi-jre/${abi}/j2re-image"/libbluray-*j2se-*.jar; \
      cp -p /opt/bdj-jars/*.jar "/opt/kodi-jre/${abi}/j2re-image/"; \
      test -s "/opt/kodi-jre/${abi}/j2re-image/libbluray-j2se-${LIBBLURAY_VERSION}.jar"; \
      chmod -R a+rX "/opt/kodi-jre/${abi}"; \
      rm -rf /tmp/addon.zip /tmp/addon; \
    done

# --- runtime ---------------------------------------------------------------
COPY build-kodi-android.sh /usr/local/bin/build-kodi-android
RUN chmod 0755 /usr/local/bin/build-kodi-android

# Default ABI for this tag; overridable at run time with -e KODI_ARCH=...
ARG KODI_ARCH=arm64-v8a
ENV KODI_ARCH=${KODI_ARCH}

# KODI_SRC / KODI_STATE / KODI_OUTPUT are deliberately NOT set here. The
# entrypoint defaults them to /src, /state and /output, and setting them as ENV
# would make them indistinguishable from a deliberate override - which is what
# KODI_DATA needs to detect in order to derive all three from one mount.
#
# No VOLUME declaration either: it would create three anonymous volumes on every
# run of a container that uses KODI_DATA, and dangling volumes on an Unraid box
# are somebody's confusing evening.
ENV KODI_APP_PACKAGE=org.xbmc.fandangos \
    KODI_BUILD_CONFIG=Release \
    ANDROID_NDK_VERSION=${ANDROID_NDK_VERSION} \
    PUID=99 \
    PGID=100

WORKDIR /

ENTRYPOINT ["/usr/local/bin/build-kodi-android"]
