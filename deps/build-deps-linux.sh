#!/bin/sh

set -e

BASEDIR=$(cd $(dirname "$0") && pwd)
PREFIX=$BASEDIR/linux
BUILDDIR=$BASEDIR/build

mkdir -p $PREFIX/include
mkdir -p $PREFIX/lib

mkdir -p $BUILDDIR

cd $BASEDIR/libjpeg-turbo
autoreconf -fiv
mkdir -p $BUILDDIR/libjpeg-turbo
cd $BUILDDIR/libjpeg-turbo
$BASEDIR/libjpeg-turbo/configure --enable-static --disable-shared --with-jpeg8 --prefix=$PREFIX
make
make install

cd $BASEDIR/zlib
if [ ! -f "./configure" ]; then
    ./autogen.sh
fi
mkdir -p $BUILDDIR/zlib
cd $BUILDDIR/zlib
$BASEDIR/zlib/configure --prefix=$PREFIX --static
make
make install

cd $BASEDIR/libpng
if [ ! -f "./configure" ]; then
    ./autogen.sh
fi
mkdir -p $BUILDDIR/libpng
cd $BUILDDIR/libpng
CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib" $BASEDIR/libpng/configure --prefix=$PREFIX --disable-shared --enable-static
make
make install

cd $BASEDIR/libwebp
if [ ! -f "./configure" ]; then
    ./autogen.sh
fi
mkdir -p $BUILDDIR/libwebp
cd $BUILDDIR/libwebp
$BASEDIR/libwebp/configure --prefix=$PREFIX --disable-shared --enable-static
make
make install

cd $BASEDIR/giflib
if [ ! -f "./configure" ]; then
    ./autogen.sh
fi
mkdir -p $BUILDDIR/giflib
cd $BUILDDIR/giflib
$BASEDIR/giflib/configure --prefix=$PREFIX --disable-shared --enable-static
make
make install

cd $BASEDIR/opencv
mkdir -p $BUILDDIR/opencv
cd $BUILDDIR/opencv
cmake $BASEDIR/opencv -DWITH_JPEG=ON -DWITH_PNG=ON -DWITH_WEBP=ON -DWITH_JASPER=OFF -DWITH_TIFF=OFF -DWITH_OPENEXR=OFF -DWITH_OPENCL=OFF -DBUILD_JPEG=OFF -DBUILD_PNG=OFF -DENABLE_SSE41=ON -DENABLE_SSE42=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_DOCS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_TESTS=OFF -DCMAKE_LIBRARY_PATH=$PREFIX/LIB -DCMAKE_INCLUDE_PATH=$PREFIX/INCLUDE -DCMAKE_INSTALL_PREFIX=$PREFIX
make
make install
