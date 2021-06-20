default_build_CC=gcc
default_build_CXX=g++
default_build_AR=ar
default_build_RANLIB=ranlib
default_build_STRIP=strip
default_build_NM=nm
default_build_OTOOL=otool
default_build_INSTALL_NAME_TOOL=install_name_tool

ALL_BUILD_TOOLS=CC CXX AR RANLIB NM STRIP SHA256SUM DOWNLOAD OTOOL INSTALL_NAME_TOOL
define add_build_tool_func
build_$(build_os)_$1 ?= $$(default_build_$1)
build_$(build_arch)_$(build_os)_$1 ?= $$(build_$(build_os)_$1)
build_$1=$(build_$(build_os)_toolchain_path)$$(build_$(build_arch)_$(build_os)_$1)
endef
$(foreach var,$(ALL_BUILD_TOOLS), $(eval $(call add_build_tool_func,$(var))))
$(info ----- BUILD TOOLS -----)
$(foreach var,$(ALL_BUILD_TOOLS), $(info build_$(var)=$(build_$(var))))

ALL_BUILD_FLAGS=CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
define add_build_flags_func
build_$(build_arch)_$(build_os)_$1 += $(build_$(build_os)_$1)
build_$1=$$(build_$(build_arch)_$(build_os)_$1)
endef

$(foreach flags,$(ALL_BUILD_FLAGS), $(eval $(call add_build_flags_func,$(flags))))
