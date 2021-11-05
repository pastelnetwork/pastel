mingw32_install_path=$(dir $(which $(host_toolchain)gcc))
mingw32_toolchain_path=$(if $(mingw32_install_path), $(mingw32_install_path),/usr/bin/)
mingw32_host_RC_COMPILER=$(host_toolchain)windres

mingw32_CFLAGS=-pipe
mingw32_CXXFLAGS=$(mingw32_CFLAGS)
mingw32_LDFLAGS=-L/usr/$(HOST)/lib

mingw32_release_CFLAGS=-O1
mingw32_release_CXXFLAGS=$(mingw32_release_CFLAGS)

mingw32_debug_CFLAGS=-O1
mingw32_debug_CXXFLAGS=$(mingw32_debug_CFLAGS)

mingw32_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC

mingw32_cmake_system=Windows
mingw32_cmake_system_version=10.0

