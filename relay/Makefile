# Phantom-Socket V3.0 - Relay Server Makefile
# Go-based TURN/STUN relay server with TLS mimicry
# Target: static binary < 5MB, CGO_ENABLED=0

VERSION ?= 3.0.0
GOOS ?= linux
GOARCH ?= $(shell go env GOARCH 2>/dev/null || echo "amd64")

# Build flags
LDFLAGS := -s -w -X main.version=$(VERSION)
BUILD_FLAGS := -ldflags "$(LDFLAGS)"

# Directories
CMD_DIR := ./cmd/relay
BIN_DIR := bin

# Binary name
TARGET := $(BIN_DIR)/phantom-relay

.PHONY: all clean release test install help \
        linux-amd64 linux-arm64 linux-armv7 all-arch

# Default target
all: $(TARGET)
	@echo "Build complete: $(TARGET)"
	@ls -lh $(TARGET) 2>/dev/null || true

# Create bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Build relay server
$(TARGET): $(BIN_DIR)
	@echo "Building relay server..."
	@CGO_ENABLED=0 GOOS=$(GOOS) GOARCH=$(GOARCH) go build $(BUILD_FLAGS) -o $(TARGET) $(CMD_DIR)
	@echo "Binary size: $$(stat -c%s $(TARGET) 2>/dev/null || stat -f%z $(TARGET) 2>/dev/null || echo 'unknown') bytes"

# Release build
release: all
	@echo "Release build complete"
	@echo "Verifying binary..."
	@file $(TARGET)

# Cross-compilation targets
linux-amd64:
	@CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build $(BUILD_FLAGS) -o $(BIN_DIR)/phantom-relay-amd64 $(CMD_DIR)
	@echo "Built linux-amd64: $(BIN_DIR)/phantom-relay-amd64"

linux-arm64:
	@CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build $(BUILD_FLAGS) -o $(BIN_DIR)/phantom-relay-arm64 $(CMD_DIR)
	@echo "Built linux-arm64: $(BIN_DIR)/phantom-relay-arm64"

linux-armv7:
	@CGO_ENABLED=0 GOOS=linux GOARCH=arm GOARM=7 go build $(BUILD_FLAGS) -o $(BIN_DIR)/phantom-relay-armv7 $(CMD_DIR)
	@echo "Built linux-armv7: $(BIN_DIR)/phantom-relay-armv7"

# Build all architectures
all-arch: linux-amd64 linux-arm64 linux-armv7
	@echo "All architectures built"

# Test target
test:
	@echo "Running relay tests..."
	@go test -v ./internal/...

# Clean build artifacts
clean:
	@echo "Cleaning relay build artifacts..."
	rm -rf $(BIN_DIR)

# Install to system
install: all
	@echo "Installing phantom-relay..."
	install -m 0755 -d /usr/local/bin
	install -m 0755 $(TARGET) /usr/local/bin/phantom-relay
	@echo "Installation complete"

# Help
help:
	@echo "Phantom-Socket V3.0 Relay Build System"
	@echo ""
	@echo "Usage:"
	@echo "  make              - Build relay server"
	@echo "  make linux-amd64  - Build for Linux amd64"
	@echo "  make linux-arm64  - Build for Linux arm64"
	@echo "  make linux-armv7  - Build for Linux ARMv7"
	@echo "  make all-arch     - Build all architectures"
	@echo "  make release      - Build release binary"
	@echo "  make test         - Run tests"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make install      - Install to /usr/local/bin"
	@echo ""
	@echo "Environment variables:"
	@echo "  VERSION           - Version string (default: 3.0.0)"
	@echo "  GOOS              - Target OS (default: linux)"
	@echo "  GOARCH            - Target architecture (auto-detected)"
