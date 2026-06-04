#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright The XCSoar Project
#
# Provision the Nickel cross-toolchain sysroot and FBInk library required for
# TARGET=KOBO_NICKEL builds.
#
# Usage:
#   ./install-kobo-nickel-toolchain.sh
#
# Environment:
#   NICKEL_TC_DIR       Install prefix (default: ~/tc, or /tc if writable)
#   NICKEL_DOCKER_IMAGE NickelTC image (default: ghcr.io/pgaskin/nickeltc:1)
#   FBINK_TAG           FBInk git tag or branch (default: master)
#   FBINK_DIR           FBInk checkout (default: repo .cache/fbink)

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

NICKEL_DOCKER_IMAGE="${NICKEL_DOCKER_IMAGE:-ghcr.io/pgaskin/nickeltc:1}"
FBINK_TAG="${FBINK_TAG:-master}"
FBINK_DIR="${FBINK_DIR:-${repo_root}/.cache/fbink}"
NICKEL_GCC_VERSION="${NICKEL_GCC_VERSION:-10}"

if [ -z "${NICKEL_TC_DIR:-}" ]; then
  if [ -d /tc/arm-nickel-linux-gnueabihf ] && [ -w /tc ]; then
    NICKEL_TC_DIR=/tc
  else
    NICKEL_TC_DIR="${HOME}/tc"
  fi
fi

nickel_prefix="${NICKEL_TC_DIR}/arm-nickel-linux-gnueabihf"
sysroot="${nickel_prefix}/arm-nickel-linux-gnueabihf/sysroot"
stamp="${nickel_prefix}/.xcsoar-provisioned"
fbink_stamp="${nickel_prefix}/.xcsoar-fbink-${FBINK_TAG}-gcc${NICKEL_GCC_VERSION}"

gcc_root="/usr/lib/gcc-cross/arm-linux-gnueabihf/${NICKEL_GCC_VERSION}"
gcc_include="/usr/lib/gcc-cross/arm-linux-gnueabihf/${NICKEL_GCC_VERSION}/include"
gcc_include_fixed="/usr/lib/gcc-cross/arm-linux-gnueabihf/${NICKEL_GCC_VERSION}/include-fixed"

nickel_cflags="-nostdinc --sysroot=${sysroot}"
nickel_cflags+=" -isystem ${gcc_include}"
nickel_cflags+=" -isystem ${gcc_include_fixed}"
nickel_cflags+=" -isystem ${sysroot}/usr/include"
nickel_cflags+=" -O2"

if [ ! -f "${sysroot}/usr/lib/libcrypto.so" ]; then
  echo "Fetching NickelTC sysroot into ${NICKEL_TC_DIR}..."
  mkdir -p "${NICKEL_TC_DIR}"
  docker pull "${NICKEL_DOCKER_IMAGE}"
  docker run --rm "${NICKEL_DOCKER_IMAGE}" \
    tar cf - -C /tc arm-nickel-linux-gnueabihf \
    | tar xf - -C "${NICKEL_TC_DIR}"
fi

if ! command -v "arm-linux-gnueabihf-gcc-${NICKEL_GCC_VERSION}" >/dev/null 2>&1; then
  echo "arm-linux-gnueabihf-gcc-${NICKEL_GCC_VERSION} not found." >&2
  echo "Run: sudo ./install-debian-packages.sh UPDATE ARM KOBO_NICKEL" >&2
  exit 1
fi

if [ ! -d "${gcc_root}" ]; then
  echo "Missing GCC ${NICKEL_GCC_VERSION} cross includes: ${gcc_root}" >&2
  exit 1
fi

if [ ! -d "${FBINK_DIR}/.git" ]; then
  echo "Cloning FBInk ${FBINK_TAG}..."
  mkdir -p "$(dirname "${FBINK_DIR}")"
  git clone --depth 1 --branch "${FBINK_TAG}" \
    --recurse-submodules \
    https://github.com/NiLuJe/FBInk.git "${FBINK_DIR}"
else
  current_ref="$(git -C "${FBINK_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
  if [ "${current_ref}" != "${FBINK_TAG}" ]; then
    echo "Updating FBInk checkout to ${FBINK_TAG}..."
    git -C "${FBINK_DIR}" fetch --depth 1 origin "${FBINK_TAG}"
    git -C "${FBINK_DIR}" checkout "${FBINK_TAG}"
    git -C "${FBINK_DIR}" submodule update --init --recursive
  else
    git -C "${FBINK_DIR}" pull --ff-only origin "${FBINK_TAG}" || true
  fi
fi

if [ ! -f "${fbink_stamp}" ] || \
   [ "${FBINK_DIR}/fbink.c" -nt "${fbink_stamp}" ]; then
  echo "Building FBInk shared library for Nickel (GCC ${NICKEL_GCC_VERSION})..."
  make -C "${FBINK_DIR}" clean
  # Host trixie ARM headers (glibc 2.41) must not leak in; Nickel ships glibc 2.18.
  make -C "${FBINK_DIR}" sharedlib MINIMAL=1 IMAGE=1 LINUX=1 \
    CC="arm-linux-gnueabihf-gcc-${NICKEL_GCC_VERSION}" \
    AR="arm-linux-gnueabihf-gcc-ar-${NICKEL_GCC_VERSION}" \
    RANLIB="arm-linux-gnueabihf-gcc-ranlib-${NICKEL_GCC_VERSION}" \
    CFLAGS="${nickel_cflags}" \
    LDFLAGS="--sysroot=${sysroot}"
  install -d "${sysroot}/usr/lib" "${sysroot}/usr/include"
  install -m 644 "${FBINK_DIR}/Release/libfbink.so.1.0.0" "${sysroot}/usr/lib/"
  ln -sf libfbink.so.1.0.0 "${sysroot}/usr/lib/libfbink.so.1"
  ln -sf libfbink.so.1.0.0 "${sysroot}/usr/lib/libfbink.so"
  install -m 644 "${FBINK_DIR}/fbink.h" "${sysroot}/usr/include/"
  date -Iseconds > "${fbink_stamp}"
fi

date -Iseconds > "${stamp}"
echo
echo "Nickel toolchain ready:"
echo "  NICKEL_TC_DIR=${NICKEL_TC_DIR}"
echo "  NICKEL_SYSROOT=${sysroot}"
echo
echo "Build XCSoar with:"
echo "  make TARGET=KOBO_NICKEL NICKEL_TC_DIR=${NICKEL_TC_DIR} USE_CCACHE=y"
