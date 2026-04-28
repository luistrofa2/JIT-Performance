#!/usr/bin/env bash
set -e

PYTHON_VERSION="3.11.13"
PYPY_VERSION="7.3.20"
PYTHON_PREFIX="/opt/python3.11"
PYPY_PREFIX="/opt/pypy3.11"

echo "Installing Python ${PYTHON_VERSION}..."
wget https://www.python.org/ftp/python/${PYTHON_VERSION}/Python-${PYTHON_VERSION}.tgz
tar -xzf Python-${PYTHON_VERSION}.tgz 
rm Python-${PYTHON_VERSION}.tgz
cd Python-${PYTHON_VERSION}

./configure --prefix=${PYTHON_PREFIX} --enable-optimizations --with-ensurepip=install
make -j$(nproc)
sudo make altinstall

cd ..

echo "Installing PyPy ${PYPY_VERSION}..."
wget https://downloads.python.org/pypy/pypy3.11-v${PYPY_VERSION}-linux64.tar.bz2
tar -xjf pypy3.11-v${PYPY_VERSION}-linux64.tar.bz2
sudo rm -rf ${PYPY_PREFIX}
sudo mv pypy3.11-v${PYPY_VERSION}-linux64 /opt/pypy3

sudo ln -sf ${PYTHON_PREFIX}/bin/python3.11 ${PYTHON_PREFIX}/bin/python3
sudo ln -sf ${PYPY_PREFIX}/bin/pypy3 ${PYPY_PREFIX}/bin/python3
