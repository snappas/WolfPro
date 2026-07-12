all: clean dependencies64 binaries-linux binaries-linux64  build-pk3

dependencies:
	cd .. && bash fetch-dependencies.sh
	mkdir -p ../build
	cp ../deps/libjpeg-turbo/build-win/libjpeg-62.dll ../build
	cp ../deps/curl-win/curl/bin/libcurl.dll ../build

dependencies64:
	cd .. && bash fetch-dependencies64.sh
	mkdir -p ../build64

binaries-linux:
	mkdir -p ../build
	cd ../build && CC=clang CFLAGS=-m32 CXXFLAGS=-m32 LDFLAGS=-m32 cmake -G Ninja -DCMAKE_LIBRARY_PATH=/usr/lib/i386-linux-gnu -DCMAKE_PREFIX_PATH=/usr/lib/i386-linux-gnu/cmake/ -DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF -DBUILD_SERVER_MOD=OFF -DBUILD_MOD_PK3=OFF .. && ninja
	cd ../build && rm -rf CMakeCache.txt  CMakeFiles build.ninja cmake_install.cmake  librenderer.a

binaries-linux64:
	mkdir -p ../build64
	cd ../build64 && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_CLIENT=ON -DINSTALL_DEFAULT_BASEDIR=`pwd` .. && ninja
	cd ../build64 && rm -rf CMakeCache.txt  CMakeFiles build.ninja cmake_install.cmake  librenderer.a

build-pk3:
	cd ../build/wolfpro && cp *.so ../../build64/wolfpro
	cd ../build64/wolfpro && zip -r wolfpro_bin.pk3 cgame*.dll cgame*.so  ui*.dll ui*.so
	cd ../build64/wolfpro && zip -r wolfpro_server.pk3 qagame*.dll qagame*.so

clean:
	rm -rf ../build
	rm -rf ../build64

clean-deps:
	rm -rf ../deps
	rm -rf ../deps64
