#!/bin/bash

set -euo pipefail

readonly INSTALLER_URL="${PH_INSTALLER_URL:-https://raw.githubusercontent.com/your-org/phantom-socket/main/deploy/install.sh}"
readonly TEMP_INSTALLER=$(mktemp /tmp/phantom-installer.XXXXXX.sh)

trap 'rm -f "${TEMP_INSTALLER}"' EXIT INT TERM

if command -v curl &> /dev/null; then
    curl -fsSL --retry 3 --retry-delay 5 -o "${TEMP_INSTALLER}" "${INSTALLER_URL}"
elif command -v wget &> /dev/null; then
    wget -q --tries=3 --timeout=30 -O "${TEMP_INSTALLER}" "${INSTALLER_URL}"
else
    echo "[ERROR] Neither curl nor wget is available"
    exit 1
fi

if [[ ! -f "${TEMP_INSTALLER}" ]]; then
    echo "[ERROR] Failed to download installer"
    exit 1
fi

chmod +x "${TEMP_INSTALLER}"

exec bash "${TEMP_INSTALLER}" "$@"
