#!/usr/bin/env bash
set -e

echo "Installing Node.js 23.8.0..."
wget https://nodejs.org/dist/v23.8.0/node-v23.8.0-linux-x64.tar.xz

tar -xf node-v23.8.0-linux-x64.tar.xz
sudo mv node-v23.8.0-linux-x64 /opt/nodejs-23.8.0

sudo ln -sf /opt/nodejs-23.8.0/bin/node /usr/local/bin/node
sudo ln -sf /opt/nodejs-23.8.0/bin/npm /usr/local/bin/npm
sudo ln -sf /opt/nodejs-23.8.0/bin/npx /usr/local/bin/npx

sudo rm node-v23.8.0-linux-x64.tar.xz
