# Copyright (C) 2026 ps5Torrent
#
# Makefile for the PS5 Torrent Downloader payload
# Uses ps5-payload-sdk for cross-compilation
#
# Prerequisites:
#   - ps5-payload-sdk installed (see README.md)
#   - PS5_PAYLOAD_SDK environment variable set

PS5_PAYLOAD_SDK ?= $(CURDIR)/.deps/ps5-payload-sdk

# Homebrew keeps versioned LLVM outside PATH on macOS. The SDK wrappers read
# LLVM_CONFIG, so discover it automatically when the user did not export it.
ifeq ($(shell uname -s),Darwin)
BREW_LLVM_PREFIX := $(shell brew --prefix llvm@18 2>/dev/null)
ifneq ($(BREW_LLVM_PREFIX),)
LLVM_CONFIG ?= $(BREW_LLVM_PREFIX)/bin/llvm-config
export LLVM_CONFIG
endif
endif

ifneq ($(wildcard $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk),)
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
$(error PS5 payload SDK not found at $(PS5_PAYLOAD_SDK). Run ./setup.sh first or set PS5_PAYLOAD_SDK)
endif

# Project name
TARGET = ps5_torrent

# Source files
SRCS = src/main.c \
       src/sha1.c \
       src/bencode.c \
       src/torrent.c \
       src/net_utils.c \
       src/tracker.c \
       src/peer_wire.c \
       src/piece_mgr.c \
       src/file_io.c \
       src/ui.c \
       src/http_server.c \
       src/torrent_mgr.c \
       src/storage_paths.c \
       src/ps5_jailbreak.c \
       src/ps5_browser.c

# Object files
OBJS = $(SRCS:.c=.o)

# Include paths
INCLUDES = -Iinclude

# Compiler flags
CFLAGS = -O2 \
         -Wall -Wextra \
         -Wno-missing-braces \
         -Wno-unused-parameter

# Linker flags
LDLIBS = -lufs -lSceSystemService -lSceUserService

all: $(TARGET).elf

# Keep the embedded dashboard in sync with the editable HTML source.
src/web_content.h: src/web/index.html scripts/embed_web.py
	python3 scripts/embed_web.py $< $@

src/main.o: src/web_content.h

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Link the ELF payload
$(TARGET).elf: $(OBJS) Makefile
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS) $(LDLIBS)
	@echo "=== Build complete: $(TARGET).elf ==="
	@ls -lh $(TARGET).elf

# Deploy to PS5 (requires PS5_HOST and PS5_PORT env vars)
test: $(TARGET).elf
	@if [ -z "$(PS5_HOST)" ] || [ -z "$(PS5_PORT)" ]; then \
		echo "Please set PS5_HOST and PS5_PORT environment variables"; \
		exit 1; \
	fi
	@$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(TARGET).elf
	@echo "Payload sent to $(PS5_HOST):$(PS5_PORT)"

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET).elf

# Deep clean
distclean: clean
	rm -rf *.elf *.o *~

.PHONY: all clean distclean test
