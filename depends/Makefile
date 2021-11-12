.NOTPARALLEL :

SHELL=/bin/bash
SOURCES_PATH ?= $(BASEDIR)/sources
BASE_CACHE ?= $(BASEDIR)/built
NO_WALLET ?=
PRIORITY_DOWNLOAD_PATH ?= https://download.z.cash/depends-sources

BUILD ?= $(shell ./config.guess)
HOST ?= $(BUILD)
JOBCOUNT ?= $(shell nproc)
PATCHES_PATH = $(BASEDIR)/patches
BASEDIR = $(CURDIR)
HASH_LENGTH:=11
DOWNLOAD_CONNECT_TIMEOUT:=10
DOWNLOAD_RETRIES:=3
CRATE_REGISTRY:=vendored-sources
NO_PROTON ?=

host:=$(BUILD)
ifneq ($(HOST),)
host:=$(HOST)
host_toolchain:=$(HOST)-
endif

ifneq ($(DEBUG),)
release_type=debug
else
release_type=release
endif

base_build_dir=$(BASEDIR)/work/build
base_staging_dir=$(BASEDIR)/work/staging
base_download_dir=$(BASEDIR)/work/download
canonical_host:=$(shell ./config.sub $(HOST))
build:=$(shell ./config.sub $(BUILD))

build_arch =$(firstword $(subst -, ,$(build)))
build_vendor=$(word 2,$(subst -, ,$(build)))
full_build_os:=$(subst $(build_arch)-$(build_vendor)-,,$(build))
build_os:=$(findstring linux,$(full_build_os))
build_os+=$(findstring darwin,$(full_build_os))
build_os:=$(strip $(build_os))
ifeq ($(build_os),)
build_os=$(full_build_os)
endif

host_arch=$(firstword $(subst -, ,$(canonical_host)))
host_vendor=$(word 2,$(subst -, ,$(canonical_host)))
full_host_os:=$(subst $(host_arch)-$(host_vendor)-,,$(canonical_host))
host_os:=$(findstring linux,$(full_host_os))
host_os+=$(findstring darwin,$(full_host_os))
host_os+=$(findstring mingw32,$(full_host_os))
host_os:=$(strip $(host_os))
ifeq ($(host_os),)
host_os=$(full_host_os)
endif

ifeq ($(build_os),$(host_os))
host_toolchain:=
endif

$(host_arch)_$(host_os)_prefix=$(BASEDIR)/$(host)
$(host_arch)_$(host_os)_host=$(host)
host_prefix=$($(host_arch)_$(host_os)_prefix)
build_prefix=$(host_prefix)/native
build_host=$(build)

AT_$(V):=
AT_:=@
AT:=$(AT_$(V))

.PHONY: all install
all: install

include hosts/$(host_os).mk
include hosts/default.mk
include builders/$(build_os).mk
include builders/default.mk
include packages/packages.mk

wallet_packages_$(NO_WALLET) = $(wallet_packages)
proton_packages_$(NO_PROTON) = $(proton_packages)

packages += $($(host_arch)_$(host_os)_packages) $($(host_os)_packages) $(rust_packages) $(proton_packages_) $(wallet_packages_)
native_packages += $($(host_arch)_$(host_os)_native_packages) $($(host_os)_native_packages)
all_packages = $(packages) $(native_packages)

meta_depends = Makefile funcs.mk builders/default.mk hosts/default.mk hosts/$(host_os).mk builders/$(build_os).mk cargo-checksum.sh

$(host_arch)_$(host_os)_native_binutils?=$($(host_os)_native_binutils)
$(host_arch)_$(host_os)_native_toolchain?=$($(host_os)_native_toolchain)

include funcs.mk

binutils_path=$($($(host_arch)_$(host_os)_native_binutils)_prefixbin)
toolchain_path=$($($(host_arch)_$(host_os)_native_toolchain)_prefixbin)
final_build_id_long+=$(shell $(build_SHA256SUM) config.site.in)
final_build_id+=$(shell echo -n $(final_build_id_long) | $(build_SHA256SUM) | cut -c-$(HASH_LENGTH))
$(host_prefix)/.stamp_$(final_build_id): $(native_packages) $(packages)
	$(AT)@echo "Final build id: $(final_build_id)"
	$(AT)if test ! -f "$(host_prefix)/.stamp_skip_extract"; then \
	  $(AT)rm -rf $(@D); \
	  $(AT)mkdir -p $(@D); \
	  $(AT)echo copying packages: $^; \
	  $(AT)echo to: $(@D); \
	  $(AT)cd $(@D); $(foreach package,$^, echo "Extracting package $(package) [$($(package)_cached)]..."; tar xf $($(package)_cached); ) \
	$(AT)fi
	$(AT)touch $@

$(host_prefix)/share/config.site : config.site.in $(host_prefix)/.stamp_$(final_build_id) | check-packages
	$(AT)@mkdir -p $(@D)
	$(AT)sed -e 's|@HOST@|$(host)|' \
            -e 's|@CC@|$(toolchain_path)$(host_CC)|' \
            -e 's|@CXX@|$(toolchain_path)$(host_CXX)|' \
            -e 's|@AR@|$(toolchain_path)$(host_AR)|' \
            -e 's|@RANLIB@|$(toolchain_path)$(host_RANLIB)|' \
            -e 's|@NM@|$(toolchain_path)$(host_NM)|' \
            -e 's|@STRIP@|$(toolchain_path)$(host_STRIP)|' \
            -e 's|@build_os@|$(build_os)|' \
            -e 's|@host_os@|$(host_os)|' \
            -e 's|@CFLAGS@|$(strip $(host_CFLAGS) $(host_$(release_type)_CFLAGS))|' \
            -e 's|@CXXFLAGS@|$(strip $(host_CXXFLAGS) $(host_$(release_type)_CXXFLAGS))|' \
            -e 's|@CPPFLAGS@|$(strip $(host_CPPFLAGS) $(host_$(release_type)_CPPFLAGS))|' \
            -e 's|@LDFLAGS@|$(strip $(host_LDFLAGS) $(host_$(release_type)_LDFLAGS))|' \
            -e 's|@no_wallet@|$(NO_WALLET)|' \
            -e 's|@debug@|$(DEBUG)|' \
            $< > $@
	$(AT)touch $@

define check_or_remove_cached
  echo "  >> checking cached package [$(package), hash - $($(package)_build_id)]"; \
  pkg_cached_dir="$(BASE_CACHE)/$(host)/$(package)"; \
  mkdir -p "$$pkg_cached_dir" && cd "$$pkg_cached_dir"; \
  pkg_chksum_file="$($(package)_cached_checksum)"; \
  pkg_chksum_filename=`basename $$pkg_chksum_file`; \
  $(build_SHA256SUM) -c "$$pkg_chksum_filename" >/dev/null 2>/dev/null || \
    (\
	echo "... checksum mismatch for [$(package)] package. Forcing rebuild..."; \
	rm -f "$$pkg_chksum_file" "$($(package)_cached)"; \
    )
endef

define check_or_remove_sources
  echo "  >> checking package source [$(package), hash - $($(package)_build_id)]"; \
  pkg_srcdir="$($(package)_source_dir)"; \
  mkdir -p "$$pkg_srcdir" && cd "$$pkg_srcdir"; \
  pkg_fetched="$($(package)_fetched)"; \
  if test ! -f "$$pkg_fetched"; then \
	echo "... [$$pkg_fetched] - package source hash not found"; \
  fi; \
  $(build_SHA256SUM) -c "$$pkg_fetched" >/dev/null 2>/dev/null || \
    {\
	bSrcFileMissing=0; \
	missing_source_files=; \
	for f in $($(package)_all_sources); do \
		if test ! -f "$$f"; then \
			bSrcFileMissing=1; \
			missing_source_files+=$$f ; \
		fi; \
	done; \
	if [ $$bSrcFileMissing -eq 1 ]; then \
		echo "... checksum missing or mismatched for [$(package)] package source file(s) [$$missing_source_files]. Forcing re-download..."; \
		rm -f $($(package)_all_sources) $$pkg_fetched; \
	fi; \
    }
endef

.PHONY: check-packages check-sources show-build-info
show-build-info:
	@echo "Build info: os=[$(full_build_os)/$(build_os)], target os=[$(canonical_host)/$(full_host_os)/$(host_os)]"
	@echo "Target dir: [$(host_prefix)]"
	@echo "MAKEFLAGS: [$(MAKEFLAGS)]"
	@echo "Job count: [$(JOBCOUNT)]"

check-sources: | show-build-info
	@echo "ALL PACKAGES: [$(all_packages)]"
	@$(foreach package,$(all_packages),$(call check_or_remove_sources,$(package));)

check-packages: | check-sources
	@$(foreach package,$(all_packages),$(call check_or_remove_cached,$(package));)

install: check-packages $(host_prefix)/share/config.site

.PHONY: download-one download-osx download-linux download-win download
download-one: check-sources $(all_sources)

download-osx:
	@$(MAKE) -s HOST=x86_64-apple-darwin14 download-one
download-linux:
	@$(MAKE) -s HOST=x86_64-unknown-linux-gnu download-one
download-win:
	@$(MAKE) -s HOST=x86_64-w64-mingw32 download-one
download: download-osx download-linux download-win
