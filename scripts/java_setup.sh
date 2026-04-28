#!/usr/bin/env bash
set -e

echo "Installing Java 23.0.2..."
wget https://download.oracle.com/java/23/archive/jdk-23.0.2_linux-x64_bin.deb

sudo dpkg -i jdk-23.0.2_linux-x64_bin.deb

rm jdk-23.0.2_linux-x64_bin.deb
