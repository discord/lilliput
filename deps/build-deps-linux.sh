#!/bin/sh

set -e

BASEDIR=$(cd $(dirname "$0") && pwd)
PREFIX="$BASEDIR/linux"
BUILDDIR="$BASEDIR/build"
SRCDIR="$BASEDIR/lilliput-dep-source"

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

if [ ! -d "$SRCDIR" ]; then
    git clone https://github.com/discord/lilliput-dep-source "$SRCDIR"
fi

echo '\n--------------------'
echo 'Building libjpeg-turbo'
echo '--------------------\n'
mkdir -p $BASEDIR/libjpeg-turbo
tar -xzf $SRCDIR/libjpeg-turbo-2.1.4.tar.gz -C $BASEDIR/libjpeg-turbo --strip-components 1
cd $BASEDIR/libjpeg-turbo
mkdir -p $BUILDDIR/libjpeg-turbo
cd $BUILDDIR/libjpeg-turbo
cmake $BASEDIR/libjpeg-turbo -DENABLE_STATIC=1 -DENABLE_SHARED=0 -DWITH_JPEG8=1 -DCMAKE_INSTALL_PREFIX=$PREFIX
make
make install

echo '\n--------------------'
echo 'Building zlib'
echo '--------------------\n'
mkdir -p $BASEDIR/zlib
tar -xzf $SRCDIR/zlib-accel.tar.gz -C $BASEDIR/zlib --strip-components 1
mkdir -p $BUILDDIR/zlib
cd $BUILDDIR/zlib
$BASEDIR/zlib/configure --prefix=$PREFIX --static
make
make install

echo '\n--------------------'
echo 'Building libpng'
echo '--------------------\n'
mkdir -p $BASEDIR/libpng
tar -xzf $SRCDIR/libpng-1.6.38.tar.gz -C $BASEDIR/libpng --strip-components 1
mkdir -p $BUILDDIR/libpng
cd $BUILDDIR/libpng
CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib" $BASEDIR/libpng/configure --prefix=$PREFIX --disable-shared --enable-static --disable-unversioned-links --disable-unversioned-libpng-pc --enable-intel-sse
make
make install

echo '\n--------------------'
echo 'Building libwebp'
echo '--------------------\n'
mkdir -p $BASEDIR/libwebp
tar -xzf $SRCDIR/libwebp-a35ea50d-2023-09-12.tar.gz -C $BASEDIR/libwebp --strip-components 1
cd $BASEDIR/libwebp
./autogen.sh
mkdir -p $BUILDDIR/libwebp
cd $BUILDDIR/libwebp
$BASEDIR/libwebp/configure --prefix=$PREFIX --disable-shared --enable-static
make
make install

echo '\n--------------------'
echo 'Building giflib'
echo '--------------------\n'
mkdir -p $BASEDIR/giflib
tar -xzf $SRCDIR/giflib-5.2.1.tar.gz -C $BASEDIR/giflib --strip-components 1
mkdir -p $BUILDDIR/giflib
cd $BASEDIR/giflib
make
cp libgif.a "$PREFIX/lib"
cp gif_lib.h "$PREFIX/include"

echo '\n--------------------'
echo 'Building opencv'
echo '--------------------\n'
mkdir -p $BASEDIR/opencv
tar -xzf $SRCDIR/opencv-3.2.0.tar.gz -C $BASEDIR/opencv --strip-components 1
cd $BASEDIR/opencv
patch -p1 < $SRCDIR/0001-export-exif-orientation.patch
patch -p1 < $BASEDIR/patches/0001-remove-invalid-flow-control.patch
mkdir -p $BUILDDIR/opencv
cd $BUILDDIR/opencv
cmake $BASEDIR/opencv -DWITH_JPEG=ON -DWITH_PNG=ON -DWITH_WEBP=ON -DWITH_JASPER=OFF -DWITH_TIFF=OFF -DWITH_OPENEXR=OFF -DWITH_OPENCL=OFF -DBUILD_JPEG=OFF -DBUILD_PNG=OFF -DBUILD_ZLIB=OFF -DENABLE_SSE41=ON -DENABLE_SSE42=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_DOCS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_TESTS=OFF -DBUILD_opencv_photo=OFF -DBUILD_opencv_video=OFF -DBUILD_opencv_videoio=OFF -DBUILD_opencv_highgui=OFF -DBUILD_opencv_ml=off -DBUILD_opencv_flann=off -DBUILD_opencv_java=OFF -DBUILD_opencv_python=OFF -DCMAKE_LIBRARY_PATH=$PREFIX/LIB -DCMAKE_INCLUDE_PATH=$PREFIX/INCLUDE -DCMAKE_INSTALL_PREFIX=$PREFIX
make
make install

echo '\n--------------------'
echo 'Building bzip2'
echo '--------------------\n'
mkdir -p $BASEDIR/bzip2
tar -xvf $SRCDIR/bzip2-1.0.8.tar.gz -C $BASEDIR/bzip2 --strip-components 1
cd $BASEDIR/bzip2
make PREFIX=$PREFIX install

echo '\n--------------------'
echo 'Building ffmpeg'
echo '--------------------\n'
mkdir -p $BASEDIR/ffmpeg
tar -xjf $SRCDIR/ffmpeg-5.1.1.tar.bz2 -C $BASEDIR/ffmpeg --strip-components 1
mkdir -p $BUILDDIR/ffmpeg
cd $BUILDDIR/ffmpeg
$BASEDIR/ffmpeg/configure --prefix=$PREFIX --disable-doc --disable-programs --disable-everything --enable-demuxer=mov --enable-demuxer=matroska --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-decoder=mpeg4 --enable-decoder=h264 --enable-decoder=hevc --enable-decoder=vp9 --enable-decoder=vp8 --enable-decoder=flac --enable-decoder=mp3 --enable-decoder=aac --enable-decoder=vorbis --disable-iconv --disable-cuda --disable-cuvid --disable-nvenc --disable-xlib
make
make install

rm -rf $BASEDIR/linux/$ARCH/bin
rm -f $BASEDIR/linux/$ARCH/**/*.cmake

# Since go modules don't currently download symlinked files
# (see https://github.com/golang/go/issues/39417)
# we replace symlinks with copies of the target.
# We use a `find -exec` with a separate file because POSIX sh
# is that much more limited than bash.
find "$PREFIX" -type l -exec "${BASEDIR}/copy-symlink-target.sh" {} \;

rm -rf $BASEDIR/linux/$ARCH/bin
rm -f $BASEDIR/linux/$ARCH/**/*.cmake

if [ -n "$CI" ]; then
  echo "CI detected, cleaning up build artifacts"
  rm -rf "$SRCDIR"
  rm -rf "$BUILDDIR"
fi
