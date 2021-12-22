package=zeromq
$(package)_version=4.3.4
$(package)_download_path=https://github.com/zeromq/libzmq/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=c593001a89f5a85dd2ddf564805deb860e02471171b3f204944857336295c3e5

define $(package)_set_vars
$(package)_cxxflags+=-std=c++17
$(package)_cxxflags_linux=-fPIC
$(package)_cmake_opts_mingw32= -D_WIN32_WINNT=0x0603
$(package)_cmake_opts+= -DCMAKE_CXX_STANDARD=17
$(package)_cmake_opts+= -DCMAKE_CXX_STANDARD_REQUIRED=ON
$(package)_cmake_opts+= -DCMAKE_CXX_EXTENSIONS=OFF
$(package)_cmake_opts+= -DCMAKE_POSITION_INDEPENDENT_CODE=ON
$(package)_cmake_opts+= -DENABLE_CURVE=OFF
$(package)_cmake_opts+= -DWITH_DOC=OFF
$(package)_cmake_opts+= -DBUILD_SHARED=OFF
$(package)_cmake_opts+= -DBUILD_TESTS=OFF
$(package)_cmake_opts+= -DCMAKE_INSTALL_PREFIX=$($(package)_staging_prefix_dir)
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
  $(MAKE) -j$(JOBCOUNT)
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_dir)$(host_prefix)/lib && \
  $(MAKE) -C ./build/ VERBOSE=1 install && \
  install ./build/lib/libzmq.a $($(package)_staging_dir)$(host_prefix)/lib/libzmq.a && \
  cp -a ./include $($(package)_staging_dir)$(host_prefix)/
endef

