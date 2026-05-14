#!/bin/bash
# Build verification script — run BEFORE every git push

set -e

echo "[-] Verifying build artifacts..."

# Check relay
if [ ! -f "bin/gs-oyen-r" ]; then
    echo "[ERROR] Relay binary missing: bin/gs-oyen-r"
    exit 1
fi

if command -v file >/dev/null 2>&1; then
    RELAY_TYPE=$(file bin/gs-oyen-r)
    if [[ "$RELAY_TYPE" != *"ELF 64-bit"* ]]; then
        echo "[ERROR] Relay is NOT Linux ELF: $RELAY_TYPE"
        exit 1
    fi
    echo "[+] Relay: Linux ELF verified via file command."
fi

# Check magic bytes (universal)
RELAY_MAGIC=$(od -An -t x1 -N 4 bin/gs-oyen-r | tr -d ' ')
if [[ "$RELAY_MAGIC" != "7f454c46" ]]; then
    echo "[ERROR] Relay magic bytes wrong: $RELAY_MAGIC (expected 7f454c46)"
    exit 1
fi
echo "[+] Relay: ELF magic bytes verified ($RELAY_MAGIC)."

# Check client
if [ ! -f "bin/gs-oyen-s-x86_64" ]; then
    echo "[ERROR] Client binary missing: bin/gs-oyen-s-x86_64"
    exit 1
fi

if command -v file >/dev/null 2>&1; then
    CLIENT_TYPE=$(file bin/gs-oyen-s-x86_64)
    if [[ "$CLIENT_TYPE" != *"ELF 64-bit"* ]]; then
        echo "[ERROR] Client is NOT Linux ELF: $CLIENT_TYPE"
        exit 1
    fi
    echo "[+] Client: Linux ELF verified via file command."
fi

CLIENT_MAGIC=$(od -An -t x1 -N 4 bin/gs-oyen-s-x86_64 | tr -d ' ')
if [[ "$CLIENT_MAGIC" != "7f454c46" ]]; then
    echo "[ERROR] Client magic bytes wrong: $CLIENT_MAGIC (expected 7f454c46)"
    exit 1
fi
echo "[+] Client: ELF magic bytes verified ($CLIENT_MAGIC)."

echo "[+] ALL BUILD ARTIFACTS VERIFIED. Safe to push."
