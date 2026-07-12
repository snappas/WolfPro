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
VER=$(curl --silent -qI https://github.com/libjpeg-turbo/libjpeg-turbo/releases/latest | awk -F '/' '/^location/ {print  substr($NF, 1, length($NF)-1)}');
wget https://api.github.com/repos/libjpeg-turbo/libjpeg-turbo/tarball/$VER
tar xvfz $VER
rm $VER
mv *libjpeg-turbo* libjpeg-turbo
cd $LIBJPEG_DIR
mkdir build
cd build
cmake -G"Unix Makefiles" ..
make -j
cp ../*.h .
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









