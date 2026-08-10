#!/bin/bash

mkdir -p deps64
cd deps64
DEPS_ROOT=`pwd`


JANSSON_DIR=`pwd`/jansson
if [ ! -d "$JANSSON_DIR" ]; then
VER=$(curl --silent -qI https://github.com/akheron/jansson/releases/latest | awk -F '/' '/^location/ {print  substr($NF, 1, length($NF)-1)}');
wget https://api.github.com/repos/akheron/jansson/tarball/$VER
tar xvfz $VER
rm $VER
mv *jansson* jansson
cd $JANSSON_DIR
mkdir build
cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM="3.5" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY  -DJANSSON_BUILD_DOCS=OFF
ninja
fi
cd $DEPS_ROOT

LIBJPEG_DIR=`pwd`/libjpeg-turbo
if [ ! -d "$LIBJPEG_DIR" ]; then
# Pinned: don't auto-track "latest" here, it silently pulls in breaking
# releases (e.g. 3.2.0's SIMD dispatcher rewrite required a matching fix
# in tr_image.c). Bump deliberately and retest JPEG texture loading.
VER=3.2.0
wget https://api.github.com/repos/libjpeg-turbo/libjpeg-turbo/tarball/$VER
tar xvfz $VER
rm $VER
mv *libjpeg-turbo* libjpeg-turbo
cd $LIBJPEG_DIR
mkdir build
cd build
cmake -G"Unix Makefiles" -DWITH_TURBOJPEG=OFF -DENABLE_SHARED=OFF ..
make -j
cp ../*.h .
cp ../src/*.h .
fi
cd $DEPS_ROOT

LIBLZMA_DIR=`pwd`/xz
if [ ! -d "$LIBLZMA_DIR" ]; then
# Pinned: XZ Utils 5.8.3, the current stable release tag as of this writing
# (https://github.com/tukaani-project/xz/releases) -- avoid "latest" per
# this project's fetch-dependencies convention.
VER=5.8.3
wget https://github.com/tukaani-project/xz/releases/download/v${VER}/xz-${VER}.tar.gz
tar xvfz xz-${VER}.tar.gz
rm xz-${VER}.tar.gz
mv xz-${VER} xz
cd $LIBLZMA_DIR
mkdir build
cd build
# WolfPro only ever uses the LZMA2 filter, HC4 match finder and CRC32 check
# (see sv_wtvdemo.c / cl_wtvdemo.c) -- trimming the rest keeps liblzma's
# filter-dispatch tables from pulling every BCJ/delta/matchfinder object into
# the final binary.
cmake -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_NLS=OFF -DXZ_THREADS=no -DXZ_ENCODERS="lzma1;lzma2" -DXZ_DECODERS="lzma1;lzma2" -DXZ_MATCH_FINDERS=hc4 -DXZ_CHECKS=crc32 -DXZ_MICROLZMA_ENCODER=OFF -DXZ_MICROLZMA_DECODER=OFF -DXZ_LZIP_DECODER=OFF ..
make -j
cp ../src/liblzma/api/lzma.h .
cp -r ../src/liblzma/api/lzma .
fi
cd $DEPS_ROOT

OMNIBOT_DIR=`pwd`/omni-bot
if [ ! -d "$OMNIBOT_DIR" ]; then
VER=$(curl --silent -qI https://github.com/jswigart/omni-bot/releases/latest | awk -F '/' '/^location/ {print  substr($NF, 1, length($NF)-1)}');
wget https://api.github.com/repos/jswigart/omni-bot/tarball/$VER
tar xvfz $VER
rm $VER
mv *omni-bot* omni-bot
fi









