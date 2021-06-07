package=native_clang
$(package)_major_version=11
$(package)_version=11.1.0
$(package)_download_path=https://github.com/llvm/llvm-project/releases/download/llvmorg-$($(package)_version)
$(package)_download_path_linux=https://github.com/llvm/llvm-project/releases/download/llvmorg-$($(package)_version)
$(package)_download_file_linux=clang+llvm-$($(package)_version)-x86_64-linux-gnu-ubuntu-16.04.tar.xz
$(package)_file_name_linux=clang-llvm-$($(package)_version)-x86_64-linux-gnu-ubuntu-16.04.tar.xz
$(package)_sha256_hash_linux=c691a558967fb7709fb81e0ed80d1f775f4502810236aa968b4406526b43bee1
$(package)_download_path_darwin=https://github.com/llvm/llvm-project/releases/download/llvmorg-$($(package)_version)-rc2
$(package)_download_file_darwin=clang+llvm-$($(package)_version)-rc2-x86_64-apple-darwin.tar.xz
$(package)_file_name_darwin=clang-llvm-$($(package)_version)-rc2-x86_64-apple-darwin.tar.xz
$(package)_sha256_hash_darwin=a7c7ec4535ced2bf881f77b4549a2d016315e63e524646239e9047b6ed5eb0be
$(package)_dependencies=

# Ensure we have clang native to the builder, not the target host
ifneq ($(canonical_host),$(build))
$(package)_exact_download_path=$($(package)_download_path_$(build_os))
$(package)_exact_download_file=$($(package)_download_file_$(build_os))
$(package)_exact_file_name=$($(package)_file_name_$(build_os))
$(package)_exact_sha256_hash=$($(package)_sha256_hash_$(build_os))
endif

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/bin && \
  cp bin/clang-$($(package)_major_version) $($(package)_staging_prefix_dir)/bin && \
  cp bin/{lld,llvm-ar,llvm-config,llvm-nm,llvm-lib,llvm-cxxdump,llvm-cxxfilt,llvm-objcopy,llvm-objdump,llvm-dlltool} $($(package)_staging_prefix_dir)/bin && \
  cp -P bin/{clang,clang++,ld.lld,ld64.lld,lld-link,llvm-ranlib,llvm-strip,dsymutil} $($(package)_staging_prefix_dir)/bin && \
  mv include/ $($(package)_staging_prefix_dir) && \
  mv lib/ $($(package)_staging_prefix_dir) && \
  mv libexec/ $($(package)_staging_prefix_dir)
endef
