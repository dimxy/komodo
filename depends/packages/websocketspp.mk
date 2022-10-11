package=websocketpp
$(package)_version=0.8.2
$(package)_download_path=https://github.com/zaphoyd/websocketpp/archive
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=6ce889d85ecdc2d8fa07408d6787e7352510750daa66b5ad44aacb47bea76755
$(package)_download_file=$($(package)_version).tar.gz

define $(package)_stage_cmds
  mkdir $($(package)_staging_dir)$(host_prefix)/include && \
  cp -a ./websocketpp $($(package)_staging_dir)$(host_prefix)/include
endef
