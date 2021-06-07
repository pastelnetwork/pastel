package=bdb
$(package)_version=6.2.23
$(package)_download_path=https://download.oracle.com/berkeley-db
$(package)_file_name=db-$($(package)_version).tar.gz
$(package)_sha256_hash=47612c8991aa9ac2f6be721267c8d3cdccf5ac83105df8e50809daea24e95dc7
$(package)_build_subdir=build_unix
$(package)_patches=winioctl-and-atomic_init_db.patch

define $(package)_set_vars
$(package)_config_opts=--disable-shared --enable-cxx --disable-replication --enable-option-checking
$(package)_config_opts_mingw32=--enable-mingw
$(package)_config_opts_linux=--with-pic
ifneq ($(build_os),darwin)
$(package)_config_opts_darwin=--disable-atomicsupport
endif
$(package)_config_opts_aarch64=--disable-atomicsupport
$(package)_cxxflags+=-std=c++17
endef

define $(package)_preprocess_cmds
  patch -p1 <$($(package)_patch_dir)/winioctl-and-atomic_init_db.patch && \
  if test "$(host_os)" == "mingw32" && test -f "/usr/$(HOST)/lib/libwinpthread-1.dll"; then \
    cp -vf "/usr/$(HOST)/lib/libwinpthread-1.dll" "$($(package)_build_dir)/"; \
  fi
endef

define $(package)_config_cmds
  ../dist/$($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -j$(JOBCOUNT) libdb_cxx-6.2.a libdb-6.2.a
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_lib install_include
endef
