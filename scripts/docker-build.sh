#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${LIBRATBAG_BUILD_IMAGE:-libratbag-build}"
ubuntu_version="${UBUNTU_VERSION:-26.04}"
builddir="${BUILD_DIR:-builddir-docker}"
destdir="${DESTDIR:-_docker-install}"
prefix="${PREFIX:-/usr}"
tests="${TESTS:-true}"
documentation="${DOCUMENTATION:-false}"

case "$(uname -m)" in
    x86_64) default_libdir="lib/x86_64-linux-gnu" ;;
    aarch64|arm64) default_libdir="lib/aarch64-linux-gnu" ;;
    armv7l) default_libdir="lib/arm-linux-gnueabihf" ;;
    *) default_libdir="lib" ;;
esac
libdir="${LIBDIR:-${default_libdir}}"

docker build \
    --build-arg "UBUNTU_VERSION=${ubuntu_version}" \
    -f "${repo_root}/Dockerfile.build" \
    -t "${image}:${ubuntu_version}" \
    "${repo_root}"

docker run --rm \
    -u "$(id -u):$(id -g)" \
    -v "${repo_root}:/src" \
    -w /src \
    "${image}:${ubuntu_version}" \
    bash -lc "
        set -euo pipefail
        rm -rf '${destdir}'
        meson setup '${builddir}' --prefix='${prefix}' --libdir='${libdir}' -Dtests='${tests}' -Ddocumentation='${documentation}' --reconfigure || \
            meson setup '${builddir}' --prefix='${prefix}' --libdir='${libdir}' -Dtests='${tests}' -Ddocumentation='${documentation}'
        meson compile -C '${builddir}'
        if [ '${tests}' = true ]; then
            meson test -C '${builddir}' --print-errorlogs
        fi
        DESTDIR=\"/src/${destdir}\" meson install -C '${builddir}'
    "

cat <<EOF

Build complete.

Staged files are in:
  ${repo_root}/${destdir}

To install them onto this machine, run:
  sudo ${repo_root}/scripts/docker-install.sh
EOF
