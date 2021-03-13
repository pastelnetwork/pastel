msvc_CFLAGS=
msvc_CXXFLAGS=$(mingw32_CFLAGS)

msvc_release_CFLAGS=-O1
msvc_release_CXXFLAGS=$(msvc_release_CFLAGS)

msvc_debug_CFLAGS=-O3
msvc_debug_CXXFLAGS=$(msvc_debug_CFLAGS)

msvc_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC
