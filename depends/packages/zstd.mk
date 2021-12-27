package=zstd
$(package)_version=1.5.0
$(package)_download_path=https://github.com/facebook/zstd/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=5194fbfa781fcf45b98c5e849651aa7b3b0a008c6b72d4a0db760f3002291e94

define $(package)_preprocess_cmds
  mkdir -p bld
endef

define $(package)_set_vars
$(package)_cxxflags+=-std=c++17
$(package)_cxxflags_linux=-fPIC
$(package)_cmake_opts+= -DCMAKE_CXX_STANDARD=17
$(package)_cmake_opts+= -DCMAKE_CXX_STANDARD_REQUIRED=ON
$(package)_cmake_opts+= -DCMAKE_POSITION_INDEPENDENT_CODE=ON
$(package)_cmake_opts+= -DZSTD_BUILD_SHARED=OFF
$(package)_cmake_opts+= -DZSTD_BUILD_PROGRAMS=OFF
$(package)_cmake_opts+= -DSYSINSTALL_BINDINGS=ON
$(package)_cmake_opts+= -DCMAKE_INSTALL_PREFIX=$($(package)_staging_prefix_dir)
endef

define $(package)_config_cmds
  cd bld && \
  $($(package)_cmake) ../build/cmake
endef

define $(package)_build_cmds
  cd bld && \
  $(MAKE) -j$(JOBCOUNT)
endef

define $(package)_stage_cmds
  $(MAKE) -C ./bld/ -j$(JOBCOUNT) VERBOSE=1 install
endef
