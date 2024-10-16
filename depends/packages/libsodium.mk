package=libsodium
$(package)_version=1.0.20
$(package)_download_path=https://download.libsodium.org/libsodium/releases/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
$(package)_dependencies=
$(package)_config_opts=

define $(package)_set_vars
$(package)_cflags+=-mno-avx512f
$(package)_cflags_darwin=-mno-avx
$(package)_config_env=CC="$($(package)_cc)" CFLAGS="$($(package)_cflags)"
$(package)_config_opts =--enable-static
$(package)_config_opts+=--disable-shared
$(package)_config_opts_linux+=--enable-opt
$(package)_config_opts_mingw32+=--enable-opt
$(package)_config_opts_release=--disable-debug
$(package)_config_opts_debug=--enable-debug
$(package)_cxxflags+=-std=c++20
endef

define $(package)_preprocess_cmds
  cd $($(package)_build_subdir); ./autogen.sh
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -j$(JOBCOUNT)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
