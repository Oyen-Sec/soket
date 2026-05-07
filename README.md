<p align="center">
  <img src="https://readme-typing-svg.herokuapp.com?font=Inter&weight=900&size=45&pause=1000&color=00F2FF&center=true&vCenter=true&width=600&height=100&lines=SOKET.IO;UNIFIED+SOCKET+INFRASTRUCTURE" alt="Soket.io Header" />
</p>

<p align="center">
  <img src="https://img.shields.io/badge/V1.0_STANDARD-222222?style=for-the-badge" alt="V1.0 Standard" />
  <img src="https://img.shields.io/badge/ACTIVE-00f2ff?style=for-the-badge&logoColor=black" alt="Active" />
</p>

Soket.io is a high-performance C2 infrastructure engineered for absolute resilience, stealth communication, and kernel-level monitoring. This framework provides a hardened layer for advanced operations with a zero-trace forensic footprint.

---

### TECH STACK

**Backend Core**
<p align="left">
  <img src="https://img.shields.io/badge/Golang-00ADD8?style=for-the-badge&logo=go&logoColor=white" alt="Go" />
  <img src="https://img.shields.io/badge/C99-A8B9CC?style=for-the-badge&logo=c&logoColor=black" alt="C" />
  <img src="https://img.shields.io/badge/Musl--Libc-3EBEFF?style=for-the-badge&logo=linux&logoColor=white" alt="Musl" />
</p>

**Security & Crypto**
<p align="left">
  <img src="https://img.shields.io/badge/BearSSL-00f2ff?style=for-the-badge&logo=securityscorecard&logoColor=black" alt="BearSSL" />
  <img src="https://img.shields.io/badge/TLS_1.3-444444?style=for-the-badge&logo=lock&logoColor=white" alt="TLS" />
  <img src="https://img.shields.io/badge/Ed25519-222222?style=for-the-badge&logo=keybase&logoColor=white" alt="Ed25519" />
</p>

**Infrastructure & DevOps**
<p align="left">
  <img src="https://img.shields.io/badge/Git-F05032?style=for-the-badge&logo=git&logoColor=white" alt="Git" />
  <img src="https://img.shields.io/badge/GitHub-181717?style=for-the-badge&logo=github&logoColor=white" alt="GitHub" />
  <img src="https://img.shields.io/badge/Tailwind_CSS-38B2AC?style=for-the-badge&logo=tailwind-css&logoColor=white" alt="Tailwind" />
</p>

---

### CORE ARCHITECTURE

| Layer | Component | Technical Detail |
| :--- | :--- | :--- |
| **Agent** | Client | Pure C99, Fileless Memfd, Inotify FIM |
| **Relay** | Multiplexer | Go 1.22, L7 TLS Mimicry, Ed25519 Identity |
| **Tunnel** | Transport | WebSocket over TLS, PSK Pre-Auth |

---

### SECURITY FEATURES

- **Anti-VM Detection**: Integrated CPUID and artifact scanning to bypass sandbox environments.
- **Jittered Heartbeat**: Randomized communication intervals to defeat traffic pattern analysis.
- **L7 Mimicry**: Traffic fingerprints disguised as legitimate HTTPS (Chrome/Firefox) traffic.
- **PTY Interactive Shell**: Full terminal support via forkpty for seamless administrative control.
- **PSK Pre-Auth**: 4-byte TCP-level pre-authentication to hide infrastructure from public scans.

---

### DEPLOYMENT

**Memory-Only Installation**
```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Oyen-Sec/soket/main/deploy/install.sh)"
```

**Manual Static Build**
```bash
# Agent compilation
cd client && make clean && make

# Relay compilation
cd relay && CGO_ENABLED=0 go build -ldflags="-s -w" -o bin/phantom-relay cmd/relay/main.go
```

---

### OPERATIONAL COMMANDS

Standardized aliases for rapid deployment:

- **gs-oyen-s**: Unified agent binary deployment.
- **gs-oyen-r**: High-concurrency relay multiplexer.

---

&copy; 2026 Oyen-Sec Infrastructure. Built for absolute resilience.
