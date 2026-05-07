#!/bin/bash
# Phantom-Socket V2.0 - Static Build Verification Script

echo "[*] Verifying Static Binaries..."

BASE_DIR="/mnt/c/laragon/www/gsoket/phantom-socket"
FILES=("$BASE_DIR/client/bin/phantom-client-prod-x86_64" "$BASE_DIR/relay/bin/phantom-relay")

for FILE in "${FILES[@]}"; do
    if [ ! -f "$FILE" ]; then
        echo "[ERROR] File not found: $FILE"
        exit 1
    fi

    echo "Checking $FILE..."
    
    # Check for dynamic dependencies
    if ldd "$FILE" 2>&1 | grep -q "not a dynamic executable"; then
        echo "[SUCCESS] $(basename $FILE) is statically linked."
    else
        echo "[FAILURE] $(basename $FILE) is DYNAMICALLY linked!"
        ldd "$FILE"
        exit 1
    fi

    # Check file type
    file "$FILE" | grep -q "statically linked" && echo "[OK] File command confirms static linkage."
done

echo "[*] All binaries verified successfully."
