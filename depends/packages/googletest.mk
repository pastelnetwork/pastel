package=googletest
$(package)_version=1.10.0
$(package)_download_path=https://github.com/google/$(package)/archive/refs/tags
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_download_file=release-$($(package)_version).tar.gz
$(package)_sha256_hash=9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb

define $(package)_set_vars
$(package)_cxxflags+=-std=c++17
$(package)_cxxflags_linux=-fPIC
endef

define $(package)_preprocess_cmds
  mkdir build
endef

define $(package)_config_cmds
  cd build && \
  cmake .. -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF -DCMAKE_INSTALL_PREFIX=/ -DCMAKE_POSITION_INDEPENDENT_CODE=ON
endef

define $(package)_build_cmds
  cd build && \
  $(MAKE) VERBOSE=1
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_dir)$(host_prefix)/lib && \
  $(MAKE) -C ./build/ VERBOSE=1 DESTDIR=$($(package)_staging_prefix_dir) install && \
  install ./build/lib/libgmock.a $($(package)_staging_dir)$(host_prefix)/lib/libgmock.a && \
  install ./build/lib/libgtest.a $($(package)_staging_dir)$(host_prefix)/lib/libgtest.a && \
  cp -a ./googlemock/include $($(package)_staging_dir)$(host_prefix)/ && \
  cp -a ./googletest/include $($(package)_staging_dir)$(host_prefix)/
endef
