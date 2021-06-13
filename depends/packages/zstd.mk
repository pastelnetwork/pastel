package=zstd
$(package)_version=1.5.0
$(package)_download_path=https://github.com/facebook/zstd/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=5194fbfa781fcf45b98c5e849651aa7b3b0a008c6b72d4a0db760f3002291e94



define $(package)_build_cmds
  $(MAKE) PREFIX=$($(package)_staging_prefix_dir) lib-mt -j$(JOBCOUNT)
endef

define $(package)_stage_cmds
  $(MAKE) PREFIX=$($(package)_staging_prefix_dir) install
endef

define $(package)_postprocess_cmds
endef
