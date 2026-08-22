#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
destdir="${DESTDIR:-_docker-install}"
stage="${repo_root}/${destdir}"

if [ ! -d "${stage}/usr" ]; then
    echo "No staged install found at ${stage}/usr"
    echo "Run scripts/docker-build.sh first."
    exit 1
fi

if [ "${EUID}" -ne 0 ]; then
    echo "Please run this script with sudo so it can install into /usr."
    exit 1
fi

if [ "${SKIP_SYSTEMD:-false}" != true ] && command -v systemctl >/dev/null 2>&1; then
    systemctl stop ratbagd.service || true
fi

cp -a "${stage}/usr/." /usr/
while IFS= read -r path; do
    chown root:root "/${path#${stage}/}"
done < <(find "${stage}/usr" -mindepth 1)

if [ "${SKIP_SYSTEMD:-false}" != true ] && command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
    systemctl reload dbus.service || true
    systemctl enable ratbagd.service || true
    systemctl restart ratbagd.service || true
fi

echo "Installed staged libratbag files from ${stage}/usr into /usr."

if ! python3 -c 'import evdev' >/dev/null 2>&1; then
    echo
    echo "ratbagctl needs the host Python evdev module."
    echo "Install it with: sudo apt install python3-evdev"
fi
