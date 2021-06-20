package=libgmp
$(package)_version=6.2.1
$(package)_download_path=https://gmplib.org/download/gmp/
$(package)_file_name=gmp-$($(package)_version).tar.bz2
$(package)_sha256_hash=eae9326beb4158c386e39a356818031bd28f3124cf915f8c5b1dc4c7a36b4d7c
$(package)_dependencies=
$(package)_cxx_flags_mingw32="-static-libgcc -static-libstdc++"
$(package)_cxx_flags_ldflags="-static-libgcc -static-libstdc++"

define $(package)_set_vars
$(package)_config_opts=--enable-cxx --disable-shared
$(package)_config_opts_mingw32=CC_FOR_BUILD="$(build_$(build_os)_CC)" CPP_FOR_BUILD="$(build_$(build_os)_CXX) -E"
endef

define $(package)_config_cmds
  $($(package)_autoconf) --host=$(host) --build=$(build)
endef

define $(package)_build_cmds
  $(MAKE) -j$(JOBCOUNT) CPPFLAGS='-fPIC'
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install ; echo '=== staging find for $(package):' ; find $($(package)_staging_dir)
endef
