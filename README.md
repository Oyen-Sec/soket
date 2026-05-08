<p align="center">
  <img src="https://readme-typing-svg.herokuapp.com?font=Inter&weight=900&size=50&pause=1000&color=FFFFFF&center=true&vCenter=true&width=500&height=100&lines=BLACK+SOKET+v1.0+Final" alt="Black Soket Header" />
</p>

<p align="center">
  <img src="https://img.shields.io/badge/VERSION-v1.0_Final-FFFFFF?style=for-the-badge&labelColor=0052FF" alt="Version" />
  <img src="https://img.shields.io/badge/PRODUCTION-13.213.138.250-FFFFFF?style=for-the-badge&labelColor=0052FF" alt="Production IP" />
  <img src="https://img.shields.io/badge/STATUS-OPERATIONAL-FFFFFF?style=for-the-badge&labelColor=0052FF" alt="Status" />
</p>

Black Soket v1.0 Final is a premium systems architecture for resilient C2 infrastructure. Engineered for high-stakes environments where stealth, stability, and zero-dependency execution are non-negotiable.

---

### ARCHITECTURAL STACK

**Core Systems**
<p align="left">
  <img src="https://img.shields.io/badge/GOLANG-0052FF?style=for-the-badge&logo=go&logoColor=white" alt="Go" />
  <img src="https://img.shields.io/badge/C99_STANDARDS-0052FF?style=for-the-badge&logo=c&logoColor=white" alt="C" />
  <img src="https://img.shields.io/badge/MUSL_LIBC-0052FF?style=for-the-badge&logo=linux&logoColor=white" alt="Musl" />
</p>

**Cryptographic Protocol**
<p align="left">
  <img src="https://img.shields.io/badge/TLS_1.3-FFFFFF?style=for-the-badge&logo=lock&logoColor=0052FF" alt="TLS" />
  <img src="https://img.shields.io/badge/ED25519_IDENTITY-FFFFFF?style=for-the-badge&logo=keybase&logoColor=0052FF" alt="Ed25519" />
  <img src="https://img.shields.io/badge/PSK_PREAUTH-FFFFFF?style=for-the-badge&logo=securityscorecard&logoColor=0052FF" alt="PSK" />
</p>

---

### SYSTEM ARCHITECTURE

| Layer | Component | Technical Specification |
| :--- | :--- | :--- |
| **Transport** | Secure Tunnel | WebSocket over TLS 1.3, Encrypted Handshake |
| **Multiplexer** | Relay Node | Static Go Binary, L7 Mimicry, IP Production Locked |
| **Agent** | Client | Static C Binary, Memfd Fileless Execution, Anti-VM |

---

### PRODUCTION DEPLOYMENT

**Automated Deployment**
```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Oyen-Sec/soket/main/deploy/install.sh)"
```

**Manual Infrastructure Build**
```bash
# Production Relay Build
cd relay && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o bin/gs-oyen-r cmd/relay/main.go

# Production Agent Build
cd client && make clean && make prod
```

---

### OPERATIONAL ALIASES

- **gs-oyen-s**: Standard deployment for production agents.
- **gs-oyen-r**: High-concurrency relay multiplexer.

---

&copy; 2026 Oyen-Sec Systems Architect. Production Release v1.0 Final.
