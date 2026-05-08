#!/bin/bash
cd /mnt/c/laragon/www/gsoket/phantom-socket
echo "[*] Building gs-oyen-r v1.0 Final (Static)..."
cd relay
CGO_ENABLED=0 go build -trimpath -ldflags="-s -w -extldflags '-static'" -o bin/gs-oyen-r cmd/relay/main.go
echo "[*] Building gs-oyen-s v1.0 Final (Static)..."
cd ../client
make clean
make prod
echo "[*] Moving binaries to deploy/..."
mkdir -p ../deploy
cp bin/gs-oyen-s-x86_64 ../deploy/gs-oyen-s-x86_64
cp ../relay/bin/gs-oyen-r ../deploy/gs-oyen-r
