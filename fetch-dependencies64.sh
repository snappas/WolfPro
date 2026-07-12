#!/bin/bash

CMAKEMINGW=$(cat <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR X64)
set(CMAKE_C_COMPILER /usr/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER /usr/bin/x86_64-w64-mingw32-windres)
set(CMAKE_SHARED_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
EOF
)

CMAKECLANGCL=$(cat <<EOF
set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(MSVC_VERSION 1300)
set(MSVC_INCREMENTAL_DEFAULT ON)
add_compile_options(-fuse-ld=lld-link)
add_definitions(-DWIN32 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_XKEYCHECK_H -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
-D_WIN32_WINNT=0x0600 -DUNICODE -D_UNICODE)
add_compile_options(-fms-extensions -fms-compatibility -Wno-ignored-attributes
					-Wno-unused-local-typedef
				    -Wno-expansion-to-defined -Wno-pragma-pack -Wno-ignored-pragma-intrinsic
					-Wno-unknown-pragmas -Wno-invalid-token-paste -Wno-deprecated-declarations -Wno-macro-redefined
					-Wno-dllimport-static-field-def
					-Wno-unused-command-line-argument
					-Wno-unknown-argument
					-Wno-int-to-void-pointer-cast)
EOF
)


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

LIBUNWIND_DIR=`pwd`/libunwind
if [ ! -d "$LIBUNWIND_DIR" ]; then
VER=$(curl --silent -qI https://github.com/libunwind/libunwind/releases/latest | awk -F '/' '/^location/ {print  substr($NF, 1, length($NF)-1)}');
wget https://api.github.com/repos/libunwind/libunwind/tarball/$VER
tar xvfz $VER
rm $VER
mv *libunwind* libunwind
cd $LIBUNWIND_DIR
mkdir build
autoreconf -i
./configure --prefix=${LIBUNWIND_DIR}/build --disable-documentation --disable-tests
make -j
make install
fi
cd $DEPS_ROOT

LIBSDL_DIR=`pwd`/SDL
if [ ! -d "$LIBSDL_DIR" ]; then
VER=$(curl --silent -qI https://github.com/libsdl-org/SDL/releases/latest | awk -F '/' '/^location/ {print  substr($NF, 1, length($NF)-1)}');
wget https://api.github.com/repos/libsdl-org/SDL/tarball/$VER
tar xvfz $VER
rm $VER
mv *SDL* SDL
cd $LIBSDL_DIR
mkdir build
mkdir build-win
./autogen.sh
./configure --prefix=${LIBSDL_DIR}/build 
make -j
make install
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
mkdir build-win
cd build
cmake -G"Unix Makefiles" ..
make -j
cp ../*.h .

cd ../build-win
echo "${CMAKEMINGW}" > toolchain.cmake
cmake -G"Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=./toolchain.cmake  -DCMAKE_INSTALL_PREFIX=${LIBJPEG_DIR}/build-win ..
LDFLAGS="-static -static-libgcc -static-libstdc++" make -j
cp ../*.h .
gendef libjpeg-62.dll
x86_64-w64-mingw32-dlltool -d libjpeg-62.def -l libjpeg-62.lib
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









