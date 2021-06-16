package=zstd
$(package)_version=1.5.0
$(package)_download_path=https://github.com/facebook/zstd/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=5194fbfa781fcf45b98c5e849651aa7b3b0a008c6b72d4a0db760f3002291e94


define $(package)_preprocess_cmds
  rm -rf build/cmake/build && \
  mkdir -p build/cmake/build
endef

define $(package)_config_cmds
  cd build/cmake/build ; $($(package)_cmake) .. -DZSTD_BUILD_PROGRAMS=OFF -DSYSINSTALL_BINDINGS=ON -DCMAKE_INSTALL_PREFIX=$($(package)_staging_prefix_dir) -DCMAKE_POSITION_INDEPENDENT_CODE=ON
endef

define $(package)_build_cmds
  cd build/cmake/build ; make -j$(JOBCOUNT)
endef

define $(package)_stage_cmds
  cd build/cmake/build; $(MAKE) VERBOSE=1 install
endef

define $(package)_postprocess_cmds
endef
