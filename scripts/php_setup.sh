#!/usr/bin/env bash
set -e

PHP_VERSION="8.4.20"
INSTALL_PREFIX="/usr/local/php-$PHP_VERSION"

cd /usr/src
sudo wget https://www.php.net/distributions/php-$PHP_VERSION.tar.gz
sudo tar -xzf php-$PHP_VERSION.tar.gz
sudo rm php-$PHP_VERSION.tar.gz
cd php-$PHP_VERSION

sudo ./configure \
  --prefix=$INSTALL_PREFIX \
  --with-config-file-path=$INSTALL_PREFIX/etc \
  --with-config-file-scan-dir=$INSTALL_PREFIX/etc/conf.d \
  --enable-opcache \
  --enable-cli \
  --with-curl \
  --with-openssl \
  --with-zlib \
  --with-zip \
  --with-readline \
  --with-mysqli \
  --with-pdo-mysql \
  --enable-mbstring \
  --with-gmp \
  --enable-sysvshm \
  --enable-sysvsem \
  --enable-sysvmsg \

sudo make -j"$(nproc)"
sudo make install

sudo mkdir -p $INSTALL_PREFIX/etc/conf.d

sudo cp php.ini-production $INSTALL_PREFIX/etc/php.ini

# Enable opcache by default in ini
# cat <<EOF | sudo tee $INSTALL_PREFIX/etc/conf.d/10-opcache.ini
# zend_extension=opcache
# opcache.enable=1
# opcache.enable_cli=1
# opcache.jit=1235
# opcache.jit_buffer_size=64M
# EOF

sudo ln -sf $INSTALL_PREFIX/bin/php /usr/local/bin/php-$PHP_VERSION

echo "PHP installed: $($INSTALL_PREFIX/bin/php -v)"
