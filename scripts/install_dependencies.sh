#!/usr/bin/env bash
set -e

echo "Installing build dependencies..."

sudo apt update

sudo apt install -y \
make gcc git tar wget curl \
build-essential pkg-config re2c \
libssl-dev zlib1g-dev libgmp-dev \
libncurses5-dev libncursesw5-dev \
libreadline-dev libsqlite3-dev \
libgdbm-dev libbz2-dev libexpat1-dev \
liblzma-dev tk-dev libffi-dev \
autoconf bison rustc \
libyaml-dev libdb-dev m4 \
libwxgtk3.2-dev libgl1-mesa-dev libglu1-mesa-dev \
libpng-dev libssh-dev unixodbc-dev \
xsltproc fop libxml2-utils \
libzip-dev libxml2-dev libonig-dev \
libcurl4-openssl-dev libjpeg-dev libwebp-dev
