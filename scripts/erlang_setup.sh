sudo apt update
sudo apt install build-essential autoconf m4 libncurses5-dev \
libssl-dev libwxgtk3.2-dev libgl1-mesa-dev libglu1-mesa-dev \
libpng-dev libssh-dev unixodbc-dev xsltproc fop libxml2-utils

wget https://github.com/erlang/otp/releases/download/OTP-27.0/otp_src_27.0.tar.gz
tar -xzf otp_src_27.0.tar.gz
cd otp_src_27.0

./configure --disable-jit

make -j$(nproc)
sudo make install

