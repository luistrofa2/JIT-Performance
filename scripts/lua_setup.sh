#!/usr/bin/env bash
set -e

echo "Installing Lua 5.1.5..."
wget https://www.lua.org/ftp/lua-5.1.5.tar.gz
tar -xzf lua-5.1.5.tar.gz
cd lua-5.1.5

make linux
sudo make install

cd ..
sudo rm -rf lua-5.1.5 lua-5.1.5.tar.gz

echo "Installing LuaJIT 2.1..."
git clone https://github.com/LuaJIT/LuaJIT.git
cd LuaJIT

make -j$(nproc)
sudo make install

cd ..
sudo rm -rf LuaJIT
