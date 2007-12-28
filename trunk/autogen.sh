#!/bin/sh

autoreconf -i -s && \
  ./configure --host=arm-none-linux-gnueabi --with-libvncserver=/opt/android
