build_linux_SHA256SUM = sha256sum
build_linux_DOWNLOAD = curl --location --fail --connect-timeout $(DOWNLOAD_CONNECT_TIMEOUT) --retry $(DOWNLOAD_RETRIES) -o

build_linux_path=$(dir $(which gcc))
build_linux_toolchain_path=$(if $(linux_install_path), $(linux_install_path),/usr/bin/)
