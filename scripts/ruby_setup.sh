#!/usr/bin/env bash
set -e

# Ensure rbenv is available
if ! command -v rbenv >/dev/null; then
    git clone https://github.com/rbenv/rbenv.git ~/.rbenv
    git clone https://github.com/rbenv/ruby-build.git ~/.rbenv/plugins/ruby-build
    echo 'export PATH="$HOME/.rbenv/bin:$PATH"' >> ~/.bashrc
    echo 'eval "$(rbenv init - bash)"' >> ~/.bashrc
    source ~/.bashrc
fi

RUBY_CONFIGURE_OPTS="--enable-yjit" rbenv install 3.4.0 #IF PRISM IS NOT PRESENT IN VERSION, INSTALL WITH OPTION "--enable-prism" AS WELL

rbenv global 3.4.0

ruby -v
ruby --yjit -e "puts RubyVM::YJIT.enabled?"
