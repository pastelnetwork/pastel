OSX_MIN_VERSION=10.14
OSX_SDK_VERSION=10.14
OSX_SDK=$($(host_arch)_$(host_os)_prefix)/SDKs/MacOSX$(OSX_SDK_VERSION).sdk
LD64_VERSION=530
ifneq ($(build_os),$(host_os))
DARWIN_TOOLCHAIN_PATH=$($(host_arch)_$(host_os)_prefix)/native/bin/
else
DARWIN_TOOLCHAIN_PATH=
endif
darwin_CC=$(DARWIN_TOOLCHAIN_PATH)clang -target $(host) -mmacosx-version-min=$(OSX_MIN_VERSION) --sysroot $(OSX_SDK) -mlinker-version=$(LD64_VERSION) -B$(build_prefix)/bin
darwin_CXX=$(DARWIN_TOOLCHAIN_PATH)clang++ -target $(host) -mmacosx-version-min=$(OSX_MIN_VERSION) --sysroot $(OSX_SDK) -stdlib=libc++ -mlinker-version=$(LD64_VERSION)  -B$(build_prefix)/bin -isystem $(OSX_SDK)/usr/include/c++/v1

darwin_CFLAGS=-pipe
darwin_CXXFLAGS=$(darwin_CFLAGS)

darwin_release_CFLAGS=-O3
darwin_release_CXXFLAGS=$(darwin_release_CFLAGS)

darwin_debug_CFLAGS=-O0
darwin_debug_CXXFLAGS=$(darwin_debug_CFLAGS)

darwin_native_binutils=native_cctools macosx_sdk
darwin_native_toolchain=native_cctools macosx_sdk
