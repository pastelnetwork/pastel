package=bdb
$(package)_version=18.1.40
$(package)_download_path=https://download.oracle.com/berkeley-db
$(package)_file_name=db-$($(package)_version).tar.gz
$(package)_sha256_hash=0cecb2ef0c67b166de93732769abdeba0555086d51de1090df325e18ee8da9c8
$(package)_build_subdir=build_unix
$(package)_patches=bdb.patch

define $(package)_set_vars
$(package)_config_opts=--disable-shared
$(package)_config_opts+=--enable-cxx
$(package)_config_opts+=--disable-replication
$(package)_config_opts+=--enable-option-checking
$(package)_config_opts_mingw32=--enable-mingw
$(package)_config_opts_mingw32+=--with-mutex=POSIX/pthreads/library/x86_64/gcc-assembly
$(package)_config_opts_linux=--with-pic
$(package)_config_opts_debug=--enable-debug
$(package)_cxxflags+=-std=c++20
endef

define $(package)_preprocess_cmds
  patch -p1 <$($(package)_patch_dir)/bdb.patch; \
  if test "$(host_os)" == "mingw32"; then \
    if test -f "/usr/$(HOST)/lib/libwinpthread-1.dll"; then \
      cp -vf "/usr/$(HOST)/lib/libwinpthread-1.dll" "$($(package)_build_dir)/"; \
    fi; \
  fi
endef

define $(package)_config_cmds
  ../dist/$($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -j$(JOBCOUNT) libdb_cxx-18.1.a libdb-18.1.a
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_lib install_include
endef
