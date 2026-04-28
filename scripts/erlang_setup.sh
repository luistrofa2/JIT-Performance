#!/usr/bin/env bash
set -e

mkdir -p ../ERLANG/otp_src_27.0_no_jit
mkdir -p ../ERLANG/otp_src_27.0

wget https://github.com/erlang/otp/releases/download/OTP-27.0/otp_src_27.0.tar.gz
tar -xzf otp_src_27.0.tar.gz -C ../ERLANG/otp_src_27.0_no_jit --strip-components=1
cd ../ERLANG/otp_src_27.0_no_jit

./configure --disable-jit

echo "Building Erlang/OTP 27.0 without JIT..."
make -j$(nproc)
sudo make install

cd ../../scripts

tar -xzf otp_src_27.0.tar.gz -C ../ERLANG/otp_src_27.0 --strip-components=1
rm otp_src_27.0.tar.gz
cd ../ERLANG/otp_src_27.0

./configure

echo "Building Erlang/OTP 27.0 with JIT..."
make -j$(nproc)
sudo make install

cd ../../scripts
