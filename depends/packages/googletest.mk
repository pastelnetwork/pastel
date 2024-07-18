package=googletest
$(package)_version=1.14.0
$(package)_download_path=https://github.com/google/$(package)/archive/refs/tags
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_download_file=v$($(package)_version).tar.gz
$(package)_sha256_hash=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7

define $(package)_set_vars
$(package)_cxxflags+= -std=c++17
$(package)_cxxflags_linux= -fPIC
$(package)_cmake_opts+= -DCMAKE_CXX_STANDARD=17
$(package)_cmake_opts+= -DCMAKE_CXX_STANDARD_REQUIRED=ON
$(package)_cmake_opts+= -DCMAKE_CXX_EXTENSIONS=OFF
$(package)_cmake_opts+= -DCMAKE_POSITION_INDEPENDENT_CODE=ON
$(package)_cmake_opts_darwin= -DCMAKE_POLICY_DEFAULT_CMP0025=NEW
endef

define $(package)_preprocess_cmds
  mkdir build
endef

define $(package)_config_cmds
  cd build && \
  $($(package)_cmake) ..
endef

define $(package)_build_cmds
  cd build && \
  $(MAKE) -j$(JOBCOUNT) VERBOSE=1
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_dir)$(host_prefix)/lib && \
  $(MAKE) -C ./build/ VERBOSE=1 DESTDIR=$($(package)_staging_prefix_dir) install && \
  install ./build/lib/libgmock.a $($(package)_staging_dir)$(host_prefix)/lib/libgmock.a && \
  install ./build/lib/libgtest.a $($(package)_staging_dir)$(host_prefix)/lib/libgtest.a && \
  cp -a ./googlemock/include $($(package)_staging_dir)$(host_prefix)/ && \
  cp -a ./googletest/include $($(package)_staging_dir)$(host_prefix)/
endef
