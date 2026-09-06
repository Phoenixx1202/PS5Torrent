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
TARGET = PS5Torrent
USE_LIBTORRENT ?= 0

# Experimental libtorrent PS5 laboratory. This is intentionally separate from
# the current C engine until the port is verified on hardware.
PS5TORRENT_LIBTORRENT_VERSION ?= 2.0.12
PS5TORRENT_BOOST_VERSION ?= boost-1.84.0
LIBTORRENT_DEPS_DIR ?= $(CURDIR)/.deps/libtorrent
LIBTORRENT_DIR ?= $(LIBTORRENT_DEPS_DIR)/libtorrent-$(PS5TORRENT_LIBTORRENT_VERSION)
BOOST_DIR ?= $(LIBTORRENT_DEPS_DIR)/$(PS5TORRENT_BOOST_VERSION)
LIBTORRENT_BUILD_DIR ?= $(CURDIR)/.build/libtorrent-ps5
LIBTORRENT_LAB_ELF = $(LIBTORRENT_BUILD_DIR)/ps5torrent-libtorrent-lab.elf
LIBTORRENT_STATIC_LIB = $(LIBTORRENT_BUILD_DIR)/libtorrent-build/libtorrent-rasterbar.a
LIBTORRENT_WITH_OPENSSL ?= auto
ifeq ($(LIBTORRENT_WITH_OPENSSL),auto)
LIBTORRENT_OPENSSL_HEADER := $(firstword \
  $(wildcard $(PS5_PAYLOAD_SDK)/target/include/openssl/opensslv.h) \
  $(wildcard $(PS5_PAYLOAD_SDK)/include/openssl/opensslv.h))
LIBTORRENT_WITH_OPENSSL := $(if $(LIBTORRENT_OPENSSL_HEADER),1,0)
endif

# Source files
BASE_SRCS = src/main.c \
       src/sha1.c \
       src/bencode.c \
       src/torrent.c \
       src/net_utils.c \
       src/tracker.c \
       src/tracker_udp.c \
       src/web_seed.c \
       src/peer_wire.c \
       src/piece_mgr.c \
       src/file_io.c \
       src/ui.c \
       src/app_log.c \
       src/http_server.c \
       src/storage_paths.c \
       src/ps5_jailbreak.c \
       src/console_files.c \
       src/ps5_tile.c \
       src/tile_state.c

ifeq ($(USE_LIBTORRENT),1)
SRCS = $(BASE_SRCS)
CPP_SRCS = src/torrent_mgr_libtorrent.cpp
TARGET = PS5Torrent-libtorrent
else
SRCS = $(BASE_SRCS) src/torrent_mgr.c
CPP_SRCS =
endif

# Object files
OBJS = $(SRCS:.c=.o)
CPP_OBJS = $(CPP_SRCS:.cpp=.o)

# Include paths
INCLUDES = -Iinclude
LIBTORRENT_INCLUDES = -I$(LIBTORRENT_DIR)/include -isystem $(BOOST_DIR)

# Compiler flags
CFLAGS = -O2 \
         -Wall -Wextra \
         -Wno-missing-braces \
         -Wno-unused-parameter
CXXFLAGS = $(CFLAGS) \
           -std=c++17 \
           -fexceptions \
           -Wno-deprecated-declarations \
           -DBOOST_ASIO_ENABLE_CANCELIO \
           -DBOOST_ASIO_NO_DEPRECATED \
           -DTORRENT_DISABLE_MUTABLE_TORRENTS \
           -DTORRENT_DISABLE_STREAMING \
           -DTORRENT_NO_DEPRECATE \
           -DTORRENT_USE_I2P=0

ifeq ($(LIBTORRENT_WITH_OPENSSL),1)
CXXFLAGS += -DOPENSSL_NO_DTLS1 \
            -DOPENSSL_NO_SSL2 \
            -DOPENSSL_NO_SSL3 \
            -DOPENSSL_NO_TLS1 \
            -DOPENSSL_NO_TLS1_1 \
            -DTORRENT_SSL_PEERS \
            -DTORRENT_USE_LIBCRYPTO \
            -DTORRENT_USE_OPENSSL
endif

# Linker flags
LDLIBS = -lufs -lSceSystemService -lSceAppInstUtil
LIBTORRENT_LDLIBS = $(LIBTORRENT_STATIC_LIB) -pthread
ifeq ($(LIBTORRENT_WITH_OPENSSL),1)
LIBTORRENT_LDLIBS += -lssl -lcrypto
endif
LIBTORRENT_CMAKE_FLAGS =
ifeq ($(LIBTORRENT_WITH_OPENSSL),0)
LIBTORRENT_CMAKE_FLAGS += -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE \
                          -DCMAKE_DISABLE_FIND_PACKAGE_GnuTLS=TRUE \
                          -DCMAKE_DISABLE_FIND_PACKAGE_LibGcrypt=TRUE
endif

all: $(TARGET).elf

libtorrent-deps:
	PS5TORRENT_LIBTORRENT_VERSION=v$(PS5TORRENT_LIBTORRENT_VERSION) \
	PS5TORRENT_BOOST_VERSION=$(PS5TORRENT_BOOST_VERSION) \
	bash scripts/libtorrent/fetch-deps.sh $(LIBTORRENT_DEPS_DIR)

libtorrent-lab-configure: libtorrent-deps
	$(PS5_PAYLOAD_SDK)/bin/prospero-cmake -S src_libtorrent -B $(LIBTORRENT_BUILD_DIR) \
	  -DCMAKE_TOOLCHAIN_FILE=$(PS5_PAYLOAD_SDK)/toolchain/prospero.cmake \
	  -DLIBTORRENT_SOURCE_DIR=$(LIBTORRENT_DIR) \
	  -DBOOST_ROOT=$(BOOST_DIR) \
	  -DCMAKE_BUILD_TYPE=Release \
	  $(LIBTORRENT_CMAKE_FLAGS)

libtorrent-engine: libtorrent-lab-configure
	cmake --build $(LIBTORRENT_BUILD_DIR) --target torrent-rasterbar -j4

libtorrent-lab: libtorrent-lab-configure
	cmake --build $(LIBTORRENT_BUILD_DIR) --target ps5torrent-libtorrent-lab.elf -j4
	mkdir -p dist
	cp $(LIBTORRENT_LAB_ELF) dist/
	@echo "=== Build complete: dist/ps5torrent-libtorrent-lab.elf ==="

# Keep the embedded dashboard in sync with the editable HTML source.
src/web_content.h: src/web/index.html src/web/i18n.js scripts/embed_web.py
	python3 scripts/embed_web.py $< $@

src/main.o: src/web_content.h
src/ps5_tile.o: pkg/PS5Torrent.pkg pkg/sce_sys/param.json

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LIBTORRENT_INCLUDES) -c -o $@ $<

ifeq ($(USE_LIBTORRENT),1)
$(CPP_OBJS): libtorrent-deps
endif

# Link the ELF payload
ifeq ($(USE_LIBTORRENT),1)
$(TARGET).elf: libtorrent-engine $(OBJS) $(CPP_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(OBJS) $(CPP_OBJS) $(LDLIBS) $(LIBTORRENT_LDLIBS)
	@echo "=== Build complete: $(TARGET).elf ==="
	@ls -lh $(TARGET).elf
else
$(TARGET).elf: $(OBJS) Makefile
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS) $(LDLIBS)
	@echo "=== Build complete: $(TARGET).elf ==="
	@ls -lh $(TARGET).elf
endif

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
	rm -f $(OBJS) $(CPP_OBJS) PS5Torrent.elf PS5Torrent-libtorrent.elf

# Deep clean
distclean: clean
	rm -rf *.elf *.o *~

.PHONY: all clean distclean test libtorrent-deps libtorrent-lab-configure libtorrent-engine libtorrent-lab
