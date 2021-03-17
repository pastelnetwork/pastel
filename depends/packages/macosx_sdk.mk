package=macosx_sdk
OSX_SDK_VERSION?=10.14
$(package)_download_path=https://github.com/phracker/MacOSX-SDKs/releases/download/10.15
$(package)_version=$(OSX_SDK_VERSION)
$(package)_file_name=MacOSX$(OSX_SDK_VERSION).sdk.tar.xz
$(package)_sha256_hash=0f03869f72df8705b832910517b47dd5b79eb4e160512602f593ed243b28715f
$(package)_dependencies=

define $(package)_stage_cmds
  mkdir -p "$($(package)_staging_prefix_dir)/SDKs" && \
  cd .. && \
  mv -f "$($(package)_staging_subdir)/" "$($(package)_staging_prefix_dir)/SDKs/MacOSX$(OSX_SDK_VERSION).sdk/"
endef
