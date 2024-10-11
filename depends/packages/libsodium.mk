# =============================================================================
# Makefile for Building libsodium
# =============================================================================
#
# This Makefile automates the download, configuration, compilation, and
# installation of the libsodium library. It includes conditional compiler
# flags to ensure compatibility across different operating systems, specifically
# disabling AVX2 and AVX512f instructions on macOS (Darwin) to maintain
# compatibility with Apple Silicon using Rosetta 2.
#
# =============================================================================

# -------------------------------
# Package Information
# -------------------------------
package = libsodium
$(package)_version = 1.0.20
$(package)_download_path = https://download.libsodium.org/libsodium/releases/
$(package)_file_name = $(package)-$($(package)_version).tar.gz
$(package)_sha256_hash = ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
$(package)_dependencies =
$(package)_config_opts =

# -------------------------------
# Build Tool Paths
# -------------------------------
build_STRIP=/usr/bin/strip
build_SHA256SUM=/usr/bin/sha256sum
build_DOWNLOAD=/usr/bin/curl --location --fail --connect-timeout 10 --retry 3 -o
build_OTOOL=/usr/bin/otool
build_INSTALL_NAME_TOOL=/usr/bin/install_name_tool

# -------------------------------
# Environment Setup
# -------------------------------

# Detect the operating system and convert it to lowercase for robustness
OS := $(shell uname -s | tr '[:upper:]' '[:lower:]')

# Optional: Define the number of jobs for parallel compilation
# If JOBCOUNT is not set externally, default to the number of CPU cores
JOBCOUNT ?= $(shell getconf _NPROCESSORS_ONLN)

# -------------------------------
# Define Package-Specific Variables and Flags
# -------------------------------
define libsodium_set_vars
    # Initialize compiler flags
    $(package)_cflags += -mno-avx512f

    # Conditionally add -mno-avx2 flag if building on macOS (darwin)
    ifeq ($(OS), darwin)
        $(package)_cflags += -mno-avx2
    endif

    # Set environment variables for configuration
    $(package)_config_env = CC="$($(package)_cc)" CFLAGS="$($(package)_cflags)"

    # Configuration options
    $(package)_config_opts = --enable-static
    $(package)_config_opts += --disable-shared
    $(package)_config_opts += --disable-tests
    $(package)_config_opts += --disable-assert
    $(package)_config_opts += --disable-benchmarks
    $(package)_config_opts += --enable-opt
    $(package)_config_opts_release = --disable-debug
    $(package)_config_opts_debug = --enable-debug

    # C++ compiler flags
    $(package)_cxxflags += -std=c++20

    # Debugging: Print the CFLAGS for verification
    $(info CFLAGS for libsodium: $($(package)_cflags))
endef

# Apply the set_vars definition
$(libsodium_set_vars)

# -------------------------------
# Define Build Commands
# -------------------------------

# Preprocessing commands (e.g., running autogen.sh)
define libsodium_preprocess_cmds
	cd $($(package)_build_subdir); ./autogen.sh
endef

# Configuration commands
define libsodium_config_cmds
	$($(package)_autoconf)
endef

# Build commands
define libsodium_build_cmds
	$(MAKE) -j$(JOBCOUNT)
endef

# Stage commands (installation)
define libsodium_stage_cmds
	$(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

# -------------------------------
# Download and Verify Source
# -------------------------------
# (Assuming you have rules to handle downloading and verifying the source.
# These rules are placeholders and should be implemented as needed.)

# Example:
# $(package)_downloaded: $(package)_file_name
# 	$(build_DOWNLOAD) $@ $(package)_download_path$(package)_file_name
#
# $(package)_verified: $(package)_downloaded
# 	echo "$(package)_sha256_hash  $(package)_downloaded" | $(build_SHA256SUM) -c -

# -------------------------------
# Main Build Targets
# -------------------------------

.PHONY: all preprocess configure build stage clean

all: preprocess configure build stage

# Preprocess step
preprocess:
	$(libsodium_preprocess_cmds)

# Configure step
configure:
	$(libsodium_config_cmds)

# Build step
build:
	$(libsodium_build_cmds)

# Stage step (installation)
stage:
	$(libsodium_stage_cmds)

# Clean build artifacts
clean:
	$(MAKE) clean -C $($(package)_build_subdir)

# -------------------------------
# Additional Targets or Rules
# -------------------------------
# Add any additional custom targets or rules below as needed.
