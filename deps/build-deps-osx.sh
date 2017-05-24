#!/bin/sh

set -e

BASEDIR=$(cd $(dirname "$0") && pwd)
PREFIX=$BASEDIR/osx
BUILDDIR=$BASEDIR/build

mkdir -p $PREFIX/include
mkdir -p $PREFIX/lib

rm -rf $BUILDDIR
mkdir -p $BUILDDIR

rm -rf libjpeg-turbo
rm -rf zlib
rm -rf libpng
rm -rf libwebp
rm -rf giflib
rm -rf opencv
rm -rf bzip2
rm -rf ffmpeg

mkdir -p $BASEDIR/libjpeg-turbo
tar -xzf $BASEDIR/libjpeg-turbo-1.5.1.tar.gz -C $BASEDIR/libjpeg-turbo --strip-components 1
cd $BASEDIR/libjpeg-turbo
autoreconf -fiv
mkdir -p $BUILDDIR/libjpeg-turbo
cd $BUILDDIR/libjpeg-turbo
$BASEDIR/libjpeg-turbo/configure --enable-static --disable-shared --with-jpeg8 --prefix=$PREFIX
make
make install

mkdir -p $BASEDIR/zlib
tar -xzf $BASEDIR/zlib-1.2.11.tar.gz -C $BASEDIR/zlib --strip-components 1
mkdir -p $BUILDDIR/zlib
cd $BUILDDIR/zlib
$BASEDIR/zlib/configure --prefix=$PREFIX --static
make
make install

mkdir -p $BASEDIR/libpng
tar -xzf $BASEDIR/libpng-1.6.29.tar.gz -C $BASEDIR/libpng --strip-components 1
mkdir -p $BUILDDIR/libpng
cd $BUILDDIR/libpng
$BASEDIR/libpng/configure --prefix=$PREFIX --disable-shared --enable-static --enable-intel-sse
make
make install

mkdir -p $BASEDIR/libwebp
tar -xzf $BASEDIR/libwebp-0.6.0.tar.gz -C $BASEDIR/libwebp --strip-components 1
cd $BASEDIR/libwebp
./autogen.sh
mkdir -p $BUILDDIR/libwebp
cd $BUILDDIR/libwebp
$BASEDIR/libwebp/configure --prefix=$PREFIX --disable-shared --enable-static
make
make install

mkdir -p $BASEDIR/giflib
tar -xjf $BASEDIR/giflib-5.1.4.tar.bz2 -C $BASEDIR/giflib --strip-components 1
cd $BASEDIR/giflib
patch -p1 < $BASEDIR/0001-initialize-SColorMap-to-fix-ownership-issue.patch
patch -p1 < $BASEDIR/0001-separate-image-header-and-allocation-phases.patch
mkdir -p $BUILDDIR/giflib
cd $BUILDDIR/giflib
$BASEDIR/giflib/configure --prefix=$PREFIX --disable-shared
make
make install

mkdir -p $BASEDIR/opencv
tar -xzf $BASEDIR/opencv-3.2.0.tar.gz -C $BASEDIR/opencv --strip-components 1
cd $BASEDIR/opencv
patch -p1 < $BASEDIR/0001-export-exif-orientation.patch
mkdir -p $BUILDDIR/opencv
cd $BUILDDIR/opencv
cmake $BASEDIR/opencv -DWITH_JPEG=ON -DWITH_PNG=ON -DWITH_WEBP=ON -DWITH_JASPER=OFF -DWITH_TIFF=OFF -DWITH_OPENEXR=OFF -DWITH_OPENCL=OFF -DBUILD_JPEG=OFF -DBUILD_PNG=OFF -DBUILD_ZLIB=OFF -DENABLE_SSE41=ON -DENABLE_SSE42=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_DOCS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_TESTS=OFF -DCMAKE_LIBRARY_PATH=$PREFIX/LIB -DCMAKE_INCLUDE_PATH=$PREFIX/INCLUDE -DCMAKE_INSTALL_PREFIX=$PREFIX
make opencv_core opencv_imgproc opencv_imgcodecs
make install

mkdir -p $BASEDIR/bzip2
tar -xvf $BASEDIR/bzip2-1.0.6.tar.gz -C $BASEDIR/bzip2 --strip-components 1
cd $BASEDIR/bzip2
make PREFIX=$PREFIX install

mkdir -p $BASEDIR/ffmpeg
tar -xjf $BASEDIR/ffmpeg-3.3.1.tar.bz2 -C $BASEDIR/ffmpeg --strip-components 1
mkdir -p $BUILDDIR/ffmpeg
cd $BUILDDIR/ffmpeg
$BASEDIR/ffmpeg/configure --prefix=$PREFIX --disable-doc --disable-programs --disable-everything --enable-demuxer=mov --enable-demuxer=matroska --enable-decoder=mpeg4 --enable-decoder=h264 --enable-decoder=mpeg4 --enable-decoder=vp9 --disable-iconv
make
make install
