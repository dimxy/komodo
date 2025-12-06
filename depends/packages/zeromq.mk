package=zeromq
$(package)_version=4.3.5
$(package)_download_path=https://github.com/zeromq/libzmq/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43
$(package)_patches=stats_proxy-missing-braces.diff

define $(package)_set_vars
  $(package)_config_opts=--without-docs --disable-shared --disable-curve
  $(package)_config_opts_linux=--with-pic
  $(package)_cxxflags_darwin+=-Wno-deprecated-declarations
  $(package)_cxxflags+=-std=c++17
endef

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/stats_proxy-missing-braces.diff
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) src/libzmq.la
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install-libLTLIBRARIES install-includeHEADERS install-pkgconfigDATA
endef

define $(package)_postprocess_cmds
  rm -rf bin share
endef