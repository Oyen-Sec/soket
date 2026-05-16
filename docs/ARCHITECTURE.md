# PHANTOM-SOCKET V3.1 — GHOST PROTOCOL: ARCHITECTURE

## SERVERLESS C2 BRAIN (V3.1-GHOST-CF)

The V3.1 architecture pivots from a traditional VPS-centric model to a **Serverless C2 Brain** utilizing Cloudflare Workers. This ensures high availability, zero maintenance, and bypasses common network restrictions.

```
┌─────────────────┐      ┌──────────────────────────┐      ┌─────────────────┐
│  KALI WSL       │      │   CLOUDFLARE WORKER      │      │  TARGET (PHP)   │
│  (MONITORING)   │      │   (C2 BRAIN & RELAY)     │      │  (GHOST AGENT)  │
│                 │      │                          │      │                 │
│ phantom-cli.py  │◀────▶│ /api/agents (KV)         │◀────▶│ ghost-agent.php │
│ Web Dashboard   │      │ /api/cmd    (Queue)      │      │ (Outbound Poll) │
└─────────────────┘      │ /dashboard  (UI)         │      └─────────────────┘
                         └──────────────────────────┘
                                      │
                                      ▼
                         ┌──────────────────────────┐
                         │   AWS VPS (FALLBACK)     │
                         │   (13.213.138.250)       │
                         │                          │
                         │   - Direct TCP 8443      │
                         │   - Active if CF is down │
                         └──────────────────────────┘
```

---

## CORE COMPONENTS

### 1. Cloudflare Worker Brain
- **Primary C2**: Always-on, distributed globally via Cloudflare Edge.
- **State Management**: Uses Cloudflare KV for agent registry and command queuing.
- **Endpoints**:
  - `/checkin`: Agents announce presence and fetch commands.
  - `/result`: Agents submit command output.
  - `/api/*`: Attacker API for management.
  - `/dashboard`: Browser-based management interface.

### 2. Ghost Agent (PHP)
- **Polling Mechanism**: Outbound HTTPS POST every 30 seconds.
- **Zero File Drop**: Only overwrites or injects into existing files.
- **Fallback Logic**: Automatically switches to AWS VPS if Cloudflare is unreachable.

### 3. Kali CLI & Dashboard
- **phantom-cli.py**: Python 3 tool for automation and interactive shells.
- **Web UI**: Standalone HTML/JS dashboard for visual monitoring.

---

## COMMUNICATION FLOW

1.  **Check-in**: Agent sends POST to `/checkin` with hostname, OS, and ID.
2.  **Queue**: Worker returns any pending commands stored in KV.
3.  **Execution**: Agent executes commands (exec, read, write, cleanup).
4.  **Reporting**: Agent sends POST to `/result` with Base64 encoded output.
5.  **Monitoring**: Attacker fetches results via CLI or Dashboard.

---

## SECURITY & STEALTH

- **HMAC/Auth**: Every request requires `X-Auth` or `X-Ghost-Signature`.
- **Encryption**: TLS 1.3 provided by Cloudflare.
- **Anti-FIM**: Zero file creation policy.
- **Port Bypass**: All traffic on standard port 443 (HTTPS).
```
