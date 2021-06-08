linux_install_path=$(dir $(which gcc))
linux_toolchain_path=$(if $(linux_install_path), $(linux_install_path),/usr/bin/)

linux_CFLAGS=-pipe
linux_CXXFLAGS=$(linux_CFLAGS)

linux_release_CFLAGS=-O1
linux_release_CXXFLAGS=$(linux_release_CFLAGS)

linux_debug_CFLAGS=-O1
linux_debug_CXXFLAGS=$(linux_debug_CFLAGS)

linux_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC

# define build architecture
ARCH_i686=-m32
ARCH_x86_64=-m64

# add architecture to all tool flags
define add_arch_flags_func
$(host_arch)_linux_$(flags)+=$(ARCH_$(host_arch))
endef
$(foreach flags,CFLAGS CXXFLAGS CPPFLAGS LDFLAGS,$(eval $(call add_arch_flags_func,$(flags))))

linux_cmake_system=Linux
