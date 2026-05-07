#!/bin/bash

set -euo pipefail

DOMAIN="${PH_DOMAIN:-relay.example.com}"
EMAIL="${PH_EMAIL:-admin@example.com}"
RELAY_PORT="${PH_RELAY_PORT:-40785}"

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
    echo -e "${BLUE}[-] $1${NC}"
}

log_success() {
    echo -e "${GREEN}[+] $1${NC}"
}

log_warn() {
    echo -e "${YELLOW}[*] WARN: $1${NC}"
}

log_error() {
    echo -e "${RED}[!] ERROR: $1${NC}"
}

if [[ $EUID -ne 0 ]]; then
    log_error "This script must be run as root"
    exit 1
fi

is_local_domain() {
    local domain="$1"

    if [[ "${domain}" == "github-raw" ]] || [[ "${domain}" == "127.0.0.1" ]]; then
        return 0
    fi

    if [[ "${domain}" =~ ^10\. ]] || \
       [[ "${domain}" =~ ^192\.168\. ]] || \
       [[ "${domain}" =~ ^172\.(1[6-9]|2[0-9]|3[0-1])\. ]] || \
       [[ "${domain}" =~ ^169\.254\. ]]; then
        return 0
    fi

    return 1
}

detect_package_manager() {
    if command -v apt-get &>/dev/null; then
        echo "apt-get"
    elif command -v yum &>/dev/null; then
        echo "yum"
    elif command -v dnf &>/dev/null; then
        echo "dnf"
    elif command -v pacman &>/dev/null; then
        echo "pacman"
    elif command -v zypper &>/dev/null; then
        echo "zypper"
    else
        echo ""
    fi
}

PKG=$(detect_package_manager)

if [[ -z "${PKG}" ]]; then
    log_error "No supported package manager found"
    exit 1
fi

log_info "Detected package manager: ${PKG}"

generate_self_signed_cert() {
    local domain="$1"
    local cert_dir="/etc/letsencrypt/live/${domain}"
    local cert_path="${cert_dir}/fullchain.pem"
    local key_path="${cert_dir}/privkey.pem"

    log_info "Generating self-signed certificate for ${domain}..."

    mkdir -p "${cert_dir}"

    openssl genrsa -out "${key_path}" 2048 2>/dev/null

    openssl req -new -x509 -key "${key_path}" \
        -out "${cert_path}" \
        -days 365 \
        -nodes \
        -subj "/C=US/ST=Test/L=Test/O=Dev/CN=${domain}" 2>/dev/null

    chmod 644 "${cert_path}"
    chmod 600 "${key_path}"
    chown -R root:root "${cert_dir}"

    if [[ ! -f "${cert_path}" ]] || [[ ! -f "${key_path}" ]]; then
        log_error "Certificate generation failed - files not created"
        log_error "Cert: ${cert_path}"
        log_error "Key: ${key_path}"
        exit 1
    fi

    log_success "Self-signed certificate generated"
    log_info "Cert: ${cert_path}"
    log_info "Key: ${key_path}"
}

install_packages() {
    log_info "Installing NGINX and Certbot..."

    case "${PKG}" in
        apt-get)
            apt-get update -qq
            apt-get install -y nginx certbot python3-certbot-nginx
            ;;
        yum)
            yum install -y epel-release
            yum install -y nginx certbot python3-certbot-nginx
            ;;
        dnf)
            dnf install -y nginx certbot python3-certbot-nginx
            ;;
        pacman)
            pacman -Sy --noconfirm nginx certbot certbot-nginx
            ;;
        zypper)
            zypper install -y nginx certbot python3-certbot-nginx
            ;;
    esac

    log_success "Packages installed"
}

configure_nginx() {
    log_info "Configuring NGINX reverse proxy..."

    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
    NGINX_CONF="${PROJECT_ROOT}/configs/nginx/phantom-relay.conf"

    if [[ ! -f "${NGINX_CONF}" ]]; then
        log_error "NGINX config not found: ${NGINX_CONF}"
        exit 1
    fi

    if [[ -d /etc/nginx/sites-available ]]; then
        cp "${NGINX_CONF}" /etc/nginx/sites-available/phantom-relay.conf
        sed -i "s/your-domain.com/${DOMAIN}/g" /etc/nginx/sites-available/phantom-relay.conf
        ln -sf /etc/nginx/sites-available/phantom-relay.conf /etc/nginx/sites-enabled/

        rm -f /etc/nginx/sites-enabled/default
    elif [[ -d /etc/nginx/conf.d ]]; then
        cp "${NGINX_CONF}" /etc/nginx/conf.d/phantom-relay.conf
        sed -i "s/your-domain.com/${DOMAIN}/g" /etc/nginx/conf.d/phantom-relay.conf
    else
        log_error "NGINX configuration directory not found"
        exit 1
    fi

    local cert_path="/etc/letsencrypt/live/${DOMAIN}/fullchain.pem"
    local key_path="/etc/letsencrypt/live/${DOMAIN}/privkey.pem"

    if [[ ! -f "${cert_path}" ]]; then
        log_error "SSL certificate file not found: ${cert_path}"
        log_error "Run setup_ssl() first or generate self-signed certificate"
        exit 1
    fi

    if [[ ! -f "${key_path}" ]]; then
        log_error "SSL private key file not found: ${key_path}"
        log_error "Run setup_ssl() first or generate self-signed certificate"
        exit 1
    fi

    log_info "SSL certificate files verified"

    if id -u www-data &>/dev/null; then
        chmod 755 /etc/letsencrypt
        chmod 755 /etc/letsencrypt/live
        chmod 755 "/etc/letsencrypt/live/${DOMAIN}"
        chmod 644 "${cert_path}"
        chown -R root:www-data "/etc/letsencrypt/live/${DOMAIN}"
        log_info "Permissions set for www-data (Debian/Ubuntu)"
    elif id -u nginx &>/dev/null; then
        chmod 755 /etc/letsencrypt
        chmod 755 /etc/letsencrypt/live
        chmod 755 "/etc/letsencrypt/live/${DOMAIN}"
        chmod 644 "${cert_path}"
        chown -R root:nginx "/etc/letsencrypt/live/${DOMAIN}"
        log_info "Permissions set for nginx (CentOS/RHEL)"
    fi

    if ! nginx -t 2>&1; then
        log_error "NGINX configuration test failed"
        exit 1
    fi

    log_success "NGINX configured for ${DOMAIN}"
}

setup_ssl() {
    if is_local_domain "${DOMAIN}"; then
        log_info "Local domain detected - using self-signed certificate"
        generate_self_signed_cert "${DOMAIN}"
        return 0
    fi

    log_info "Production domain - obtaining SSL certificate from Let's Encrypt..."

    mkdir -p /var/www/certbot

    if certbot --nginx \
        -d "${DOMAIN}" \
        --email "${EMAIL}" \
        --non-interactive \
        --agree-tos \
        --redirect \
        --hsts \
        --staple-ocsp \
        --strict-permissions; then
        log_success "SSL certificate obtained from Let's Encrypt"
    else
        log_warn "Certbot failed - generating self-signed certificate as fallback"
        generate_self_signed_cert "${DOMAIN}"
    fi
}

configure_firewall() {
    log_info "Configuring firewall..."

    if command -v ufw &>/dev/null; then
        ufw allow 80/tcp
        ufw allow 443/tcp
        log_success "UFW firewall configured"
    elif command -v firewall-cmd &>/dev/null; then
        firewall-cmd --permanent --add-service=http
        firewall-cmd --permanent --add-service=https
        firewall-cmd --reload
        log_success "FirewallD configured"
    elif command -v iptables &>/dev/null; then
        iptables -A INPUT -p tcp --dport 80 -j ACCEPT
        iptables -A INPUT -p tcp --dport 443 -j ACCEPT
        log_success "iptables rules added"
    else
        log_warn "No firewall management tool found - configure manually"
    fi
}

start_services() {
    log_info "Starting services..."

    if command -v systemctl &>/dev/null; then
        systemctl restart nginx
        systemctl enable nginx
        log_success "NGINX started and enabled"
    elif command -v service &>/dev/null; then
        service nginx restart
        log_success "NGINX restarted"
    else
        log_warn "Cannot start NGINX - start manually"
    fi
}

display_summary() {
    echo ""
    log_success "=========================================="
    log_success "NGINX Reverse Proxy Deployment Complete"
    log_success "=========================================="
    echo ""
    log_info "Domain: https://${DOMAIN}"
    log_info "Relay Port: ${RELAY_PORT}"
    log_info "WebSocket: wss://${DOMAIN}/ws"
    log_info "Relay: https://${DOMAIN}/relay"
    echo ""
    log_info "Next steps:"
    log_info "1. Update client config to use: ${DOMAIN}:443"
    log_info "2. Set PH_RELAY_HOST=${DOMAIN}"
    log_info "3. Set PH_RELAY_PORT=443"
    log_info "4. Set PH_RELAY_TLS=true"
    echo ""
    log_info "Renew certificate: certbot renew --dry-run"
    log_info "View logs: tail -f /var/log/nginx/phantom-relay-*.log"
    echo ""
}

main() {
    log_info "Phantom-Socket V3.0 - Module 15 NGINX Setup"
    log_info "Domain: ${DOMAIN}"
    log_info "Email: ${EMAIL}"
    echo ""

    install_packages
    configure_nginx
    setup_ssl
    configure_firewall
    start_services
    display_summary
}

main "$@"
