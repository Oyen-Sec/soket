#!/bin/bash

set -euo pipefail

readonly SERVICE_NAME="dbus-org.freedesktop.timesync1"

readonly STEALTH_PATHS=(
    "/usr/local/share/dbus-1/system-services"
    "$HOME/.cache/fontconfig"
    "$HOME/.local/share/mime"
)

LOG_FILE="/tmp/.dbus-uninstall-log"

if [[ -t 1 ]]; then
    readonly RED='\033[0;31m'
    readonly GREEN='\033[0;32m'
    readonly YELLOW='\033[1;33m'
    readonly BLUE='\033[0;34m'
    readonly NC='\033[0m'
else
    readonly RED=''
    readonly GREEN=''
    readonly YELLOW=''
    readonly BLUE=''
    readonly NC=''
fi

log_info() {
    local msg="[-] $1"
    echo "${msg}" >> "${LOG_FILE}" 2>/dev/null || true
    echo -e "${BLUE}${msg}${NC}"
}

log_success() {
    local msg="[+] $1"
    echo "${msg}" >> "${LOG_FILE}" 2>/dev/null || true
    echo -e "${GREEN}${msg}${NC}"
}

log_warn() {
    local msg="[*] WARN: $1"
    echo "${msg}" >> "${LOG_FILE}" 2>/dev/null || true
    echo -e "${YELLOW}${msg}${NC}" >&2
}

log_error() {
    local msg="[!] ERROR: $1"
    echo "${msg}" >> "${LOG_FILE}" 2>/dev/null || true
    echo -e "${RED}${msg}${NC}" >&2
}

TEMP_FILES=()

cleanup() {
    local exit_code=$?
    for temp_file in "${TEMP_FILES[@]}"; do
        if [[ -e "${temp_file}" ]]; then
            rm -f "${temp_file}"
        fi
    done
    if [[ ${exit_code} -ne 0 ]]; then
        log_error "Uninstallation failed with exit code: ${exit_code}"
    fi
    exit ${exit_code}
}

trap cleanup EXIT INT TERM

INSTALL_DIR=""

check_installation() {
    log_info "Checking for installation..."

    for path in "${STEALTH_PATHS[@]}"; do
        if [[ -d "${path}" ]] && [[ -f "${path}/.systemd-timesyncd" ]]; then
            INSTALL_DIR="${path}"
            break
        fi
    done

    if [[ -z "${INSTALL_DIR}" ]]; then
        log_warn "Installation not found"
        exit 1
    fi
}

stop_services() {
    log_info "Stopping services..."

    if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
        systemctl stop "${SERVICE_NAME}" 2>/dev/null || true
    fi

    if systemctl --user is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
        systemctl --user stop "${SERVICE_NAME}" 2>/dev/null || true
    fi

    local pids
    pids=$(pgrep -f ".systemd-timesyncd" 2>/dev/null || true)
    if [[ -n "${pids}" ]]; then
        echo "${pids}" | while read -r pid; do
            kill -TERM "${pid}" 2>/dev/null || true
        done
        sleep 1
        pids=$(pgrep -f ".systemd-timesyncd" 2>/dev/null || true)
        if [[ -n "${pids}" ]]; then
            echo "${pids}" | while read -r pid; do
                kill -9 "${pid}" 2>/dev/null || true
            done
        fi
    fi
}

remove_systemd_service() {
    log_info "Removing services..."

    local system_service="/etc/systemd/system/${SERVICE_NAME}.service"
    if [[ -f "${system_service}" ]]; then
        systemctl disable "${SERVICE_NAME}" 2>/dev/null || true
        rm -f "${system_service}"
    fi

    local user_service="$HOME/.config/systemd/user/${SERVICE_NAME}.service"
    if [[ -f "${user_service}" ]]; then
        systemctl --user disable "${SERVICE_NAME}" 2>/dev/null || true
        rm -f "${user_service}"
    fi

    systemctl daemon-reload 2>/dev/null || true
    systemctl --user daemon-reload 2>/dev/null || true
    systemctl reset-failed "${SERVICE_NAME}" 2>/dev/null || true
}

remove_files() {
    log_info "Removing files..."

    if [[ -n "${INSTALL_DIR}" && -d "${INSTALL_DIR}" ]]; then
        rm -rf "${INSTALL_DIR}"
    fi

    rm -f /tmp/.dbus-log 2>/dev/null || true
    rm -f /tmp/.fontconfig-log 2>/dev/null || true
    rm -f /tmp/.dbus-uninstall-log 2>/dev/null || true

    rm -rf /tmp/.dbus-* 2>/dev/null || true
}

verify_removal() {
    log_info "Verifying removal..."

    local errors=0

    local pids
    pids=$(pgrep -f ".systemd-timesyncd" 2>/dev/null || true)
    if [[ -n "${pids}" ]]; then
        log_warn "Processes still running"
        errors=$((errors + 1))
    fi

    for path in "${STEALTH_PATHS[@]}"; do
        if [[ -f "${path}/.systemd-timesyncd" ]]; then
            log_warn "Binary still exists: ${path}"
            errors=$((errors + 1))
        fi
    done

    if [[ ${errors} -eq 0 ]]; then
        log_success "Removal verified"
    fi

    return ${errors}
}

display_summary() {
    echo ""
    log_success "Uninstallation complete"
    echo "[*] Log: ${LOG_FILE}"
    echo ""
}

main() {
    if [[ "${FORCE:-0}" != "1" ]]; then
        echo "[*] This will remove the siluman agent"
        read -rp "[*] Continue? [y/N] " -n 1 -r
        echo
        if [[ ! ${REPLY} =~ ^[Yy]$ ]]; then
            log_info "Cancelled"
            exit 0
        fi
    fi

    check_installation

    stop_services

    remove_systemd_service

    remove_files

    verify_removal

    display_summary
}

while [[ $
    case $1 in
        --force|-f)
            FORCE=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --force, -f   Skip confirmation prompt"
            echo "  --help, -h    Show this help message"
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

main
