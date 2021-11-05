host_toolchain_path?=$($(host_os)_toolchain_path)

default_tool_CC=gcc
default_tool_CXX=g++
default_tool_AR=ar
default_tool_RANLIB=ranlib
default_tool_RC_COMPILER=
default_tool_STRIP=strip
default_tool_LIBTOOL=libtool
default_tool_INSTALL_NAME_TOOL=install_name_tool
default_tool_OTOOL=otool
default_tool_NM=nm

ALL_HOST_TOOLS=CC CXX AR RANLIB RC_COMPILER STRIP LIBTOOL INSTALL_NAME_TOOL OTOOL NM
define add_default_host_tool_func
default_host_$1=$(host_toolchain_path)$(if $($(host_os)_host_$1),$($(host_os)_host_$1),$(host_toolchain)$(default_tool_$1))
endef
$(foreach tool,$(ALL_HOST_TOOLS),$(eval $(call add_default_host_tool_func,$(tool))))
$(info ---- DEFAULT HOST TOOLS -----)
$(foreach tool,$(ALL_HOST_TOOLS),$(info default_host_$(tool)=$(default_host_$(tool))))

define add_host_tool_func
ifneq ($(filter $(origin $1),undefined default),)
# Do not consider the well-known var $1 if it is undefined or is taking a value
# that is predefined by "make" (e.g. the make variable "CC" has a predefined
# value of "cc")
$(host_os)_$1?=$$(default_host_$1)
$(host_arch)_$(host_os)_$1?=$$($(host_os)_$1)
$(host_arch)_$(host_os)_$(release_type)_$1?=$$($(host_os)_$1)
else
$(host_os)_$1=$(or $($1),$($(host_os)_$1),$(default_host_$1))
$(host_arch)_$(host_os)_$1=$(or $($1),$($(host_arch)_$(host_os)_$1),$$($(host_os)_$1))
$(host_arch)_$(host_os)_$(release_type)_$1=$(or $($1),$($(host_arch)_$(host_os)_$(release_type)_$1),$$($(host_os)_$1))
endif
host_$1=$$($(host_arch)_$(host_os)_$1)
endef
$(foreach tool,$(ALL_HOST_TOOLS),$(eval $(call add_host_tool_func,$(tool))))
$(info ---- HOST TOOLS -----)
$(foreach tool,$(ALL_HOST_TOOLS),$(info $(host_arch)_$(host_os)_$(tool)=$($(host_arch)_$(host_os)_$(tool))))

ALL_HOST_FLAGS=CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
define add_host_flags_func
$(host_arch)_$(host_os)_$1 += $($(host_os)_$1)
$(host_arch)_$(host_os)_$(release_type)_$1 += $($(host_os)_$(release_type)_$1)
host_$1 = $$($(host_arch)_$(host_os)_$1)
host_$(release_type)_$1 = $$($(host_arch)_$(host_os)_$(release_type)_$1)
endef

$(foreach flags,$(ALL_HOST_FLAGS), $(eval $(call add_host_flags_func,$(flags))))
