#!/bin/sh

set -e

# Parse arguments
ARCH=""
for arg in "$@"; do
    case "$arg" in
        --arch=*)
            ARCH=$(echo "$arg" | sed 's/--arch=//')
            ;;
        *)
            echo "Unknown parameter: $arg"
            exit 1
            ;;
    esac
done

# Set up compilation environment based on architecture
if [ -z "$ARCH" ]; then
    # No architecture specified, use system default
    ARCH=$(uname -m)
    case "$ARCH" in
        x86_64)
            ARCH="amd64"
            ;;
        aarch64)
            ARCH="aarch64"
            ;;
        *)
            echo "Unsupported architecture: $ARCH"
            exit 1
            ;;
    esac
fi

# Set up architecture-specific flags and settings
case "$ARCH" in
    "amd64")
        export CC="${CC:-gcc}"
        export CXX="${CXX:-g++}"
        export AR="${AR:-ar}"
        export RANLIB="${RANLIB:-ranlib}"
        export STRIP="${STRIP:-strip}"
        ARCH_CFLAGS="-fPIC -O2 -I./include"
        ARCH_CXXFLAGS="-fPIC -O2 -I./include"
        CONFIGURE_HOST=
        CMAKE_CROSS_COMPILE_FLAGS=""
        FFMPEG_CROSS_COMPILE_FLAGS=""
        CPU_BASELINE="SSE2"
        OPENCV_EXTRA_FLAGS="-DENABLE_SSE41=ON -DENABLE_SSE42=ON"
        PNG_EXTRA_FLAGS="--enable-intel-sse"
        ;;
    "aarch64")
        export CC="aarch64-linux-gnu-gcc"
        export CXX="aarch64-linux-gnu-g++"
        export AR="aarch64-linux-gnu-ar"
        export RANLIB="aarch64-linux-gnu-ranlib"
        export STRIP="aarch64-linux-gnu-strip"
        ARCH_CFLAGS="-fPIC -O2 -I./include -march=armv8.2-a+dotprod+simd+i8mm"
        ARCH_CXXFLAGS="-fPIC -O2 -I./include -march=armv8.2-a+dotprod+simd+i8mm"
        CONFIGURE_HOST="aarch64-linux-gnu"
        CMAKE_CROSS_COMPILE_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_AR=/usr/bin/$AR -DCMAKE_RANLIB=/usr/bin/$RANLIB"
        FFMPEG_CROSS_COMPILE_FLAGS="--arch=aarch64 --target-os=linux --cross-prefix=aarch64-linux-gnu- --enable-cross-compile"
        CPU_BASELINE="NEON"
        OPENCV_EXTRA_FLAGS="-DENABLE_NEON=ON -DWITH_CAROTENE=ON"
        PNG_EXTRA_FLAGS=""
        ;;
    *)
        echo "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

# Function for verifying architecture
verify_arch() {
    local file="$1"
    case "$ARCH" in
        "aarch64")
            if ! readelf -h "$file" | grep -q "AArch64"; then
                echo "Error: Built library $file is not aarch64"
                exit 1
            fi
            ;;
        "amd64")
            if ! readelf -h "$file" | grep -q "Advanced Micro Devices X86-64"; then
                echo "Error: Built library $file is not x86_64"
                exit 1
            fi
            ;;
    esac
}

export MAKEFLAGS="-j$(nproc --all)"

BASEDIR=$(cd $(dirname "$0") && pwd)
PREFIX="$BASEDIR/linux/$ARCH"
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
rm -rf libyuv
rm -rf aom
rm -rf libavif

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
cmake $BASEDIR/libjpeg-turbo $CMAKE_CROSS_COMPILE_FLAGS \
    -DENABLE_STATIC=1 \
    -DENABLE_SHARED=0 \
    -DWITH_JPEG8=1 \
    -DCMAKE_INSTALL_PREFIX=$PREFIX
make
make install
verify_arch "$PREFIX/lib/libjpeg.a"

echo '\n--------------------'
echo 'Building zlib'
echo '--------------------\n'
mkdir -p $BASEDIR/zlib
tar -xzf $SRCDIR/zlib-accel.tar.gz -C $BASEDIR/zlib --strip-components 1
mkdir -p $BUILDDIR/zlib
cd $BUILDDIR/zlib
CROSS_PREFIX=${CC%gcc} $BASEDIR/zlib/configure --prefix=$PREFIX --static
make
make install
verify_arch "$PREFIX/lib/libz.a"

echo '\n--------------------'
echo 'Building libpng'
echo '--------------------\n'
mkdir -p $BASEDIR/libpng
tar -xzf $SRCDIR/libpng-1.6.38.tar.gz -C $BASEDIR/libpng --strip-components 1
mkdir -p $BUILDDIR/libpng
cd $BUILDDIR/libpng
CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib" \
$BASEDIR/libpng/configure \
    --prefix=$PREFIX \
    --disable-shared \
    --enable-static \
    --disable-unversioned-links \
    --disable-unversioned-libpng-pc \
    $PNG_EXTRA_FLAGS \
    --host=$CONFIGURE_HOST \
    CC="$CC" \
    CXX="$CXX" \
    AR="$AR" \
    RANLIB="$RANLIB"
make
make install
verify_arch "$PREFIX/lib/libpng16.a"

echo '\n--------------------'
echo 'Building libwebp'
echo '--------------------\n'
mkdir -p $BASEDIR/libwebp
tar -xzf $SRCDIR/libwebp-a35ea50d-2023-09-12.tar.gz -C $BASEDIR/libwebp --strip-components 1
cd $BASEDIR/libwebp
./autogen.sh
mkdir -p $BUILDDIR/libwebp
cd $BUILDDIR/libwebp
$BASEDIR/libwebp/configure \
    --prefix=$PREFIX \
    --disable-shared \
    --enable-static \
    --host=aarch64-linux-gnu \
    CC="$CC" \
    CXX="$CXX" \
    AR="$AR" \
    RANLIB="$RANLIB"

make
make install
verify_arch "$PREFIX/lib/libwebp.a"

echo '\n--------------------'
echo 'Building giflib'
echo '--------------------\n'
mkdir -p $BASEDIR/giflib
tar -xzf $SRCDIR/giflib-5.2.1.tar.gz -C $BASEDIR/giflib --strip-components 1
mkdir -p $BUILDDIR/giflib
cd $BASEDIR/giflib
make CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="-fPIC -O2"
cp libgif.a "$PREFIX/lib"
cp gif_lib.h "$PREFIX/include"
verify_arch "$PREFIX/lib/libgif.a"

echo '\n--------------------'
echo 'Building opencv'
echo '--------------------\n'
mkdir -p $BASEDIR/opencv
tar -xzf $SRCDIR/opencv-3.2.0.tar.gz -C $BASEDIR/opencv --strip-components 1
cd $BASEDIR/opencv
mkdir -p $BUILDDIR/opencv
cd $BUILDDIR/opencv
cmake $BASEDIR/opencv \
    $OPENCV_EXTRA_FLAGS \
    -DCPU_BASELINE="$CPU_BASELINE" \
    -DWITH_JPEG=ON \
    -DWITH_PNG=ON \
    -DWITH_WEBP=ON \
    -DWITH_JASPER=OFF \
    -DWITH_TIFF=OFF \
    -DWITH_OPENEXR=OFF \
    -DWITH_OPENCL=OFF \
    -DBUILD_JPEG=OFF \
    -DBUILD_PNG=OFF \
    -DBUILD_ZLIB=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_DOCS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_opencv_photo=ON \
    -DBUILD_opencv_video=OFF \
    -DBUILD_opencv_videoio=OFF \
    -DBUILD_opencv_highgui=ON \
    -DBUILD_opencv_ml=OFF \
    -DBUILD_opencv_dnn=OFF \
    -DBUILD_opencv_flann=OFF \
    -DBUILD_opencv_calib3d=OFF \
    -DBUILD_opencv_features2d=OFF \
    -DBUILD_opencv_objdetect=OFF \
    -DBUILD_opencv_java=OFF \
    -DBUILD_opencv_python=OFF \
    -DENABLE_PRECOMPILED_HEADERS=OFF \
    -DCMAKE_LIBRARY_PATH=$PREFIX/lib \
    -DCMAKE_INCLUDE_PATH=$PREFIX/include \
    -DCMAKE_INSTALL_PREFIX=$PREFIX
make
make install -j$(nproc --all)
verify_arch "$PREFIX/lib/libopencv_core.a"
verify_arch "$PREFIX/lib/libopencv_imgproc.a"
verify_arch "$PREFIX/lib/libopencv_imgcodecs.a"
verify_arch "$PREFIX/lib/libopencv_highgui.a"
verify_arch "$PREFIX/lib/libopencv_photo.a"

echo '\n--------------------'
echo 'Building bzip2'
echo '--------------------\n'
mkdir -p $BASEDIR/bzip2
tar -xvf $SRCDIR/bzip2-1.0.8.tar.gz -C $BASEDIR/bzip2 --strip-components 1
cd $BASEDIR/bzip2
make CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="$ARCH_CFLAGS" PREFIX=$PREFIX install
verify_arch "$PREFIX/lib/libbz2.a"

echo '\n--------------------'
echo 'Building ffmpeg'
echo '--------------------\n'
mkdir -p $BASEDIR/ffmpeg
tar -xjf $SRCDIR/ffmpeg-5.1.1.tar.bz2 -C $BASEDIR/ffmpeg --strip-components 1
mkdir -p $BUILDDIR/ffmpeg
cd $BUILDDIR/ffmpeg
$BASEDIR/ffmpeg/configure $FFMPEG_CROSS_COMPILE_FLAGS \
    --prefix=$PREFIX \
    --disable-doc \
    --disable-programs \
    --disable-everything \
    --enable-demuxer=mov \
    --enable-demuxer=matroska \
    --enable-demuxer=aac \
    --enable-demuxer=flac \
    --enable-demuxer=mp3 \
    --enable-demuxer=ogg \
    --enable-demuxer=wav \
    --enable-decoder=mpeg4 \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
    --enable-decoder=vp9 \
    --enable-decoder=vp8 \
    --enable-decoder=flac \
    --enable-decoder=mp3 \
    --enable-decoder=aac \
    --enable-decoder=vorbis \
    --disable-iconv \
    --disable-cuda \
    --disable-cuvid \
    --disable-nvenc \
    --disable-xlib
make
make install
verify_arch "$PREFIX/lib/libavcodec.a"
verify_arch "$PREFIX/lib/libavformat.a"
verify_arch "$PREFIX/lib/libavutil.a"

echo '\n--------------------'
echo 'Building libyuv'
echo '--------------------\n'
mkdir -p $BASEDIR/libyuv
tar -xzf $SRCDIR/libyuv-4ed75166cf1885b9690214b362f8675294505a37-2025-03-18.tar.gz -C $BASEDIR/libyuv --strip-components 1
cd $BASEDIR/libyuv
make V=1 -f linux.mk libyuv.a \
    CXX="$CXX" \
    CC="$CC" \
    AR="$AR" \
    CFLAGS="$ARCH_CFLAGS" \
    CXXFLAGS="$ARCH_CXXFLAGS"
cp libyuv.a "$PREFIX/lib"
cp -r include/* "$PREFIX/include/"
verify_arch "$PREFIX/lib/libyuv.a"

echo '\n--------------------'
echo 'Building libaom'
echo '--------------------\n'
mkdir -p $BASEDIR/aom
tar -xzf $SRCDIR/libaom-3.11.0.tar.gz -C $BASEDIR/aom
mkdir -p $BUILDDIR/aom
cd $BUILDDIR/aom
AOM_CMAKE_FLAGS="$CMAKE_CROSS_COMPILE_FLAGS"
if [ "$ARCH" = "aarch64" ]; then
    AOM_CMAKE_FLAGS="$AOM_CMAKE_FLAGS -DAOM_TARGET_CPU=arm64"
fi

cmake $BASEDIR/aom $AOM_CMAKE_FLAGS \
    -DENABLE_SHARED=0 \
    -DENABLE_STATIC=1 \
    -DENABLE_TESTS=0 \
    -DENABLE_TOOLS=0 \
    -DENABLE_DOCS=0 \
    -DCMAKE_INSTALL_PREFIX=$PREFIX
make
make install
verify_arch "$PREFIX/lib/libaom.a"

echo '\n--------------------'
echo 'Building libavif'
echo '--------------------\n'
mkdir -p $BASEDIR/libavif
tar -xzf $SRCDIR/libavif-1.1.1.tar.gz -C $BASEDIR/libavif --strip-components 1
mkdir -p $BUILDDIR/libavif
cd $BUILDDIR/libavif
cmake $BASEDIR/libavif $CMAKE_CROSS_COMPILE_FLAGS \
    -DAVIF_CODEC_AOM=SYSTEM \
    -DAVIF_BUILD_APPS=OFF \
    -DLIBYUV_LIBRARY=$PREFIX/lib/libyuv.a \
    -DLIBYUV_INCLUDE_DIR=$PREFIX/include \
    -DAOM_LIBRARY=$PREFIX/lib/libaom.a \
    -DAOM_INCLUDE_DIR=$PREFIX/include \
    -DJPEG_INCLUDE_DIR=$PREFIX/include \
    -DJPEG_LIBRARY=$PREFIX/lib/libjpeg.a \
    -DPNG_PNG_INCLUDE_DIR=$PREFIX/include \
    -DPNG_LIBRARY=$PREFIX/lib/libpng16.a \
    -DCMAKE_PREFIX_PATH=$PREFIX \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX=$PREFIX
make
make install
verify_arch "$PREFIX/lib/libavif.a"

# Since go modules don't currently download symlinked files
# (see https://github.com/golang/go/issues/39417)
# we replace symlinks with copies of the target.
# We use a `find -exec` with a separate file because POSIX sh
# is that much more limited than bash.
find "$PREFIX" -type l -exec "${BASEDIR}/copy-symlink-target.sh" {} \;

rm -rf $PREFIX/bin
rm -f $PREFIX/**/*.cmake

if [ -n "$CI" ]; then
  echo "CI detected, cleaning up build artifacts"
  rm -rf "$SRCDIR"
  rm -rf "$BUILDDIR"
fi
