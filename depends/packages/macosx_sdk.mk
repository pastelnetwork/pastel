package=macosx_sdk
OSX_SDK_VERSION?=10.15
$(package)_download_path=https://github.com/phracker/MacOSX-SDKs/releases/download/11.3
$(package)_version=$(OSX_SDK_VERSION)
$(package)_file_name=MacOSX$(OSX_SDK_VERSION).sdk.tar.xz
$(package)_sha256_hash=ac75d9e0eb619881f5aa6240689fce862dcb8e123f710032b7409ff5f4c3d18b
$(package)_dependencies=

define $(package)_stage_cmds
  mkdir -p "$($(package)_staging_prefix_dir)/SDKs" && \
  cd .. && \
  mv -f "$($(package)_staging_subdir)/" "$($(package)_staging_prefix_dir)/SDKs/MacOSX$(OSX_SDK_VERSION).sdk/"
endef
