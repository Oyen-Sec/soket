#!/bin/bash
# Phantom Socket v1.0.0-stable - Unified Installer Redirector
# (C) 2026 Oyen-Sec

# Handle Secret Key Logic
export X="${S:-${X:-}}"
if [[ -z "${X}" ]]; then
    X=$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 22)
fi

# Execute Installer
curl -fsSL https://raw.githubusercontent.com/Oyen-Sec/phantom-socket/main/deploy/install.sh | bash -s -- "$@"
