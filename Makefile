# SPDX-License-Identifier: GPL-3.0-or-later
# GNU Standard Makefile Wrapper for crau-nbd

BUILD_DIR ?= build
PREFIX ?= /usr/local

.PHONY: all
all:
	@cmake -B $(BUILD_DIR) -S . -DCMAKE_INSTALL_PREFIX=$(PREFIX)
	@cmake --build $(BUILD_DIR) -j$$(nproc)

.PHONY: install
install:
	@cmake --install $(BUILD_DIR)

.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR)

.PHONY: check
check: all
	@ctest --test-dir $(BUILD_DIR) --output-on-failure || true
