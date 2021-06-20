OSX_MIN_VERSION=10.14
OSX_SDK_VERSION=10.14
OSX_SDK=$($(host_arch)_$(host_os)_prefix)/SDKs/MacOSX$(OSX_SDK_VERSION).sdk
LD64_VERSION=530

darwin_host_CC = clang
darwin_host_CXX = clang++

ifneq ($(build_os),$(host_os))
darwin_toolchain_path=$($(host_arch)_$(host_os)_prefix)/native/bin/
else
darwin_toolchain_path=
endif

darwin_CFLAGS=-pipe -target $(host) -mmacosx-version-min=$(OSX_MIN_VERSION) --sysroot $(OSX_SDK) -mlinker-version=$(LD64_VERSION) -B$(build_prefix)/bin 
darwin_CXXFLAGS=$(darwin_CFLAGS) -stdlib=libc++ -isystem $(OSX_SDK)/usr/include/c++/v1

darwin_release_CFLAGS=-O3
darwin_release_CXXFLAGS=$(darwin_release_CFLAGS)

darwin_debug_CFLAGS=-O0
darwin_debug_CXXFLAGS=$(darwin_debug_CFLAGS)

darwin_native_binutils=native_cctools macosx_sdk
darwin_native_toolchain=native_cctools macosx_sdk

darwin_cmake_system=Darwin
