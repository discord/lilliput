#!/bin/sh

set -e

# Add Homebrew to PATH
export PATH="/opt/homebrew/bin:$PATH"

# Function to check and install build tools
install_build_tools() {
    for tool in automake libtool autoconf coreutils cmake; do
        if ! command -v $tool >/dev/null 2>&1; then
            echo "Installing $tool..."
            brew install $tool
        else
            echo "$tool is already installed."
        fi
    done
}

# Call the function to ensure build tools are installed
install_build_tools

BASEDIR=$(cd $(dirname "$0") && pwd)
PREFIX="$BASEDIR/osx"
BUILDDIR="$BASEDIR/build"
SRCDIR="$BASEDIR/lilliput-dep-source"

# Add architecture detection
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    export CFLAGS="-arch arm64"
    export CXXFLAGS="-arch arm64"
    export LDFLAGS="-arch arm64"
fi

mkdir -p "$PREFIX/include"
mkdir -p "$PREFIX/lib"

rm -rf "$BUILDDIR"
mkdir -p "$BUILDDIR"

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
    git clone --depth 1 --branch 1.1.0 https://github.com/discord/lilliput-dep-source "$SRCDIR"
fi

echo '\n--------------------'
echo 'Building libjpeg-turbo'
echo '--------------------\n'
mkdir -p $BASEDIR/libjpeg-turbo
tar -xzf $SRCDIR/libjpeg-turbo-2.1.4.tar.gz -C $BASEDIR/libjpeg-turbo --strip-components 1
cd $BASEDIR/libjpeg-turbo
mkdir -p $BUILDDIR/libjpeg-turbo
cd $BUILDDIR/libjpeg-turbo
cmake $BASEDIR/libjpeg-turbo \
    -DENABLE_STATIC=1 \
    -DENABLE_SHARED=0 \
    -DWITH_JPEG8=1 \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_C_FLAGS="-mmacosx-version-min=14.0" \
    -DCMAKE_CXX_FLAGS="-mmacosx-version-min=14.0"
make
make install

echo '\n--------------------'
echo 'Building zlib'
echo '--------------------\n'
mkdir -p $BASEDIR/zlib
tar -xzf $SRCDIR/zlib-1.2.11.tar.gz -C $BASEDIR/zlib --strip-components 1
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
$BASEDIR/libpng/configure --prefix=$PREFIX --disable-shared --enable-static --host=arm-apple-darwin
make
make install

echo '\n--------------------'
echo 'Building libwebp'
echo '--------------------\n'
mkdir -p $BASEDIR/libwebp
tar -xzf $SRCDIR/libwebp-a35ea50d-2023-09-12.tar.gz -C $BASEDIR/libwebp --strip-components 1
cd $BASEDIR/libwebp
# Run autogen.sh with error handling
if [ ! -f "./configure" ]; then
    if ! ./autogen.sh; then
        echo "autogen.sh failed. Trying to proceed anyway..."
    fi
fi
# If configure still doesn't exist, exit with error
if [ ! -f "./configure" ]; then
    echo "Error: ./configure script not found and couldn't be generated."
    exit 1
fi
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

# Modify the Makefile to work on macOS
sed -i '' 's/-Wno-format-truncation//g' Makefile
sed -i '' 's/-fPIC//g' Makefile
sed -i '' 's/-Wl,-soname -Wl,libgif.so.$(LIBMAJOR)//g' Makefile

# Build static library only
make libgif.a

# Copy the static library and header to the prefix
cp libgif.a "$PREFIX/lib"
cp gif_lib.h "$PREFIX/include"

echo '\n--------------------'
echo 'Building opencv'
echo '--------------------\n'
mkdir -p $BASEDIR/opencv
tar -xzf $SRCDIR/opencv-3.2.0.tar.gz -C $BASEDIR/opencv --strip-components 1
cd $BASEDIR/opencv
patch -p1 < $SRCDIR/0001-export-exif-orientation.patch

# Fix CMake configuration issues
sed -i '' 's/cmake_policy(SET CMP0026 OLD)/cmake_policy(SET CMP0026 NEW)/' CMakeLists.txt
sed -i '' 's/cmake_policy(SET CMP0022 OLD)/cmake_policy(SET CMP0022 NEW)/' CMakeLists.txt
sed -i '' 's/cmake_policy(SET CMP0020 OLD)/cmake_policy(SET CMP0020 NEW)/' CMakeLists.txt

# Add CMP0022 policy to the main CMakeLists.txt
echo "cmake_policy(SET CMP0022 NEW)" >> CMakeLists.txt

# Disable Python detection
sed -i '' 's/include(cmake\/OpenCVDetectPython.cmake)/#include(cmake\/OpenCVDetectPython.cmake)/' CMakeLists.txt

# Fix OpenCVCompilerOptions.cmake
cat > cmake/OpenCVCompilerOptions.cmake << EOL
if(CMAKE_COMPILER_IS_GNUCXX OR CV_ICC OR CV_CLANG OR MINGW)
  set(ENABLE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
endif()

set(OPENCV_EXTRA_FLAGS "")
set(OPENCV_EXTRA_C_FLAGS "")
set(OPENCV_EXTRA_CXX_FLAGS "")
set(OPENCV_EXTRA_FLAGS_RELEASE "")
set(OPENCV_EXTRA_FLAGS_DEBUG "")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS "")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS_RELEASE "")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS_DEBUG "")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
  set(ENABLE_NEON OFF)
  set(OPENCV_EXTRA_C_FLAGS "${OPENCV_EXTRA_C_FLAGS} -arch arm64")
  set(OPENCV_EXTRA_CXX_FLAGS "${OPENCV_EXTRA_CXX_FLAGS} -arch arm64")
endif()
EOL

# Create opencv.pc.in file
mkdir -p cmake/templates
cat > cmake/templates/opencv.pc.in << EOL
prefix=@prefix@
exec_prefix=@exec_prefix@
libdir=@libdir@
includedir=@includedir@

Name: OpenCV
Description: Open Source Computer Vision Library
Version: @OPENCV_VERSION@
Libs: -L\${libdir} @OPENCV_PC_LIBS@
Libs.private: @OPENCV_PC_LIBS_PRIVATE@
Cflags: -I\${includedir}
EOL

# Update highgui CMakeLists.txt
cat > modules/highgui/CMakeLists.txt << EOL
set(the_description "High-level GUI")

set(OPENCV_HIGHGUI_DEPS opencv_core opencv_imgproc opencv_imgcodecs)
ocv_add_module(highgui ${OPENCV_HIGHGUI_DEPS})

ocv_module_include_directories()
ocv_glob_module_sources()

ocv_create_module()

ocv_add_accuracy_tests()
ocv_add_perf_tests()
EOL

# Fix OpenCVGenPkgconfig.cmake
cat > cmake/OpenCVGenPkgconfig.cmake << EOL
# Generate .pc file
set(prefix      "\${CMAKE_INSTALL_PREFIX}")
set(exec_prefix "\${prefix}")
set(libdir      "\${exec_prefix}/lib")
set(includedir  "\${prefix}/include")

set(OPENCV_PC_LIBS "")
foreach(lib \${OPENCV_MODULES_PUBLIC})
  if(TARGET \${lib})
    set(OPENCV_PC_LIBS "\${OPENCV_PC_LIBS} -l\${lib}")
  endif()
endforeach()

if(BUILD_SHARED_LIBS)
  set(OPENCV_PC_LIBS_PRIVATE "")
else()
  set(OPENCV_PC_LIBS_PRIVATE \${OPENCV_EXTRA_COMPONENTS})
endif()

configure_file("\${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/opencv.pc.in"
               "\${CMAKE_BINARY_DIR}/unix-install/opencv.pc" @ONLY)

install(FILES "\${CMAKE_BINARY_DIR}/unix-install/opencv.pc" DESTINATION "\${OPENCV_LIB_INSTALL_PATH}/pkgconfig")
EOL

# Update imgcodecs CMakeLists.txt
cat > modules/imgcodecs/CMakeLists.txt << EOL
set(the_description "Image I/O")
ocv_define_module(imgcodecs opencv_core OPTIONAL opencv_imgproc WRAP java python)

set(OPENCV_IMGCODECS_LIBRARIES "")

if(HAVE_JPEG)
  list(APPEND OPENCV_IMGCODECS_LIBRARIES ${JPEG_LIBRARIES})
endif()

if(HAVE_WEBP)
  list(APPEND OPENCV_IMGCODECS_LIBRARIES ${WEBP_LIBRARIES})
endif()

if(HAVE_PNG)
  list(APPEND OPENCV_IMGCODECS_LIBRARIES ${PNG_LIBRARIES})
endif()

if(HAVE_TIFF)
  list(APPEND OPENCV_IMGCODECS_LIBRARIES ${TIFF_LIBRARIES})
endif()

if(HAVE_JASPER)
  list(APPEND OPENCV_IMGCODECS_LIBRARIES ${JASPER_LIBRARIES})
endif()

if(HAVE_GDCM)
  list(APPEND OPENCV_IMGCODECS_LIBRARIES ${GDCM_LIBRARIES})
endif()

target_link_libraries(opencv_imgcodecs PRIVATE ${OPENCV_IMGCODECS_LIBRARIES})
EOL

mkdir -p $BUILDDIR/opencv
cd $BUILDDIR/opencv
cmake $BASEDIR/opencv \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_C_FLAGS="-arch arm64 -I$PREFIX/include" \
    -DCMAKE_CXX_FLAGS="-arch arm64 -I$PREFIX/include -stdlib=libc++ -std=c++11" \
    -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++" \
    -DCMAKE_SHARED_LINKER_FLAGS="-stdlib=libc++" \
    -DCMAKE_MODULE_LINKER_FLAGS="-stdlib=libc++" \
    -DWITH_JPEG=ON \
    -DJPEG_INCLUDE_DIR=$PREFIX/include \
    -DJPEG_LIBRARY=$PREFIX/lib/libjpeg.a \
    -DWITH_PNG=ON \
    -DPNG_PNG_INCLUDE_DIR=$PREFIX/include \
    -DPNG_LIBRARY=$PREFIX/lib/libpng.a \
    -DWITH_WEBP=ON \
    -DWEBP_INCLUDE_DIR=$PREFIX/include \
    -DWEBP_LIBRARY=$PREFIX/lib/libwebp.a \
    -DWITH_JASPER=OFF \
    -DWITH_TIFF=OFF \
    -DWITH_OPENEXR=OFF \
    -DWITH_OPENCL=OFF \
    -DWITH_LAPACK=OFF \
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
    -DBUILD_opencv_highgui=OFF \
    -DBUILD_opencv_ml=OFF \
    -DBUILD_opencv_flann=OFF \
    -DBUILD_opencv_java=OFF \
    -DBUILD_opencv_python=OFF \
    -DOPENCV_FP16_DISABLE=ON \
    -DCMAKE_LIBRARY_PATH=$PREFIX/lib \
    -DCMAKE_INCLUDE_PATH=$PREFIX/include \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DWITH_AVFOUNDATION=OFF \
    -DWITH_COCOA=OFF \
    -DWITH_QUICKTIME=OFF \
    -DBUILD_APPLE_FRAMEWORK=OFF \
    -DWITH_IOS=OFF \
    -DWITH_APPLE_VISION=OFF \
    -DWITH_COREIMAGE=OFF \
    -DWITH_CAROTENE=OFF \
    -DWITH_VIDEOTOOLBOX=ON \
    -DBUILD_opencv_java=OFF \
    -DBUILD_opencv_python=OFF \
    -DCMAKE_CXX_STANDARD=11 \
    -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DCMAKE_CXX_EXTENSIONS=OFF

# Remove iOS-specific build files
sed -i '' "s|;$BASEDIR/opencv/modules/imgcodecs/include/opencv2/imgcodecs/ios.h||" $BASEDIR/build/opencv/CMakeCache.txt
rm -f $BASEDIR/opencv/modules/imgcodecs/src/ios_conversions.mm
sed -i '' "\|$BUILDDIR/opencv/modules/imgcodecs/src/ios_conversions\.mm;|d" $BUILDDIR/opencv/modules/imgcodecs/Makefile


make opencv_core opencv_imgproc opencv_imgcodecs
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
tar -xJf $SRCDIR/ffmpeg-7.0.2.orig.tar.xz -C $BASEDIR/ffmpeg --strip-components 1
mkdir -p $BUILDDIR/ffmpeg
cd $BUILDDIR/ffmpeg
$BASEDIR/ffmpeg/configure --prefix=$PREFIX --disable-doc --disable-programs --disable-everything --enable-demuxer=mov --enable-demuxer=matroska --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-decoder=mpeg4 --enable-decoder=h264 --enable-decoder=hevc --enable-decoder=vp9 --enable-decoder=vp8 --enable-decoder=flac --enable-decoder=mp3 --enable-decoder=aac --enable-decoder=vorbis --disable-iconv --arch=arm64 --enable-cross-compile --target-os=darwin
make
make install

echo '\n--------------------'
echo 'Building libyuv'
echo '--------------------\n'
mkdir -p $BASEDIR/libyuv
tar -xzf $SRCDIR/libyuv-eb6e7bb63738e29efd82ea3cf2a115238a89fa51-2024-12-12.tar.gz -C $BASEDIR/libyuv
cd $BASEDIR/libyuv
patch -p0 < $BASEDIR/patches/0002-fix-libyuv-cmake-for-osx.patch
mkdir -p $BUILDDIR/libyuv
cd $BUILDDIR/libyuv

cmake $BASEDIR/libyuv \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_C_FLAGS="-arch arm64 -fPIC -O3 -march=armv8-a+crc+crypto -mtune=apple-m1" \
    -DCMAKE_CXX_FLAGS="-arch arm64 -fPIC -O3 -march=armv8-a+crc+crypto -mtune=apple-m1 -std=c++11" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DCMAKE_PREFIX_PATH=$PREFIX \
    -DJPEG_LIBRARY=$PREFIX/lib/libjpeg.a \
    -DJPEG_INCLUDE_DIR=$PREFIX/include \
    -DCMAKE_EXE_LINKER_FLAGS="-L$PREFIX/lib" \
    -DCMAKE_SHARED_LINKER_FLAGS="-L$PREFIX/lib" \
    -DLIBYUV_BUILD_SHARED_LIBS=OFF \
    -DLIBYUV_DISABLE_SHARED=ON \
    -DLIBYUV_ENABLE_STATIC=ON

make
make install

# Remove any dylib if it was created
rm -f $PREFIX/lib/libyuv.dylib

# macOS 15 needs more specific include paths and nostdinc++
CXX_FLAGS="-O3 -march=armv8-a+crc+crypto -mtune=apple-m1 -stdlib=libc++ -std=c++11 -nostdinc++ -isystem /Library/Developer/CommandLineTools/usr/include/c++/v1 -isystem /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1 -isystem /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include"

echo '\n--------------------'
echo 'Building libaom'
echo '--------------------\n'
mkdir -p $BASEDIR/aom
tar -xzf $SRCDIR/libaom-3.11.0.tar.gz -C $BASEDIR/aom
mkdir -p $BUILDDIR/aom
cd $BUILDDIR/aom
cmake $BASEDIR/aom \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_C_FLAGS="-O3 -march=armv8-a+crc+crypto -mtune=apple-m1" \
    -DCMAKE_CXX_FLAGS="$CXX_FLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++" \
    -DCMAKE_SHARED_LINKER_FLAGS="-stdlib=libc++" \
    -DCMAKE_MODULE_LINKER_FLAGS="-stdlib=libc++" \
    -DENABLE_SHARED=0 \
    -DENABLE_STATIC=1 \
    -DENABLE_TESTS=0 \
    -DENABLE_TOOLS=0 \
    -DENABLE_DOCS=0 \
    -DENABLE_NEON=1 \
    -DENABLE_VSX=0 \
    -DCONFIG_MULTITHREAD=1 \
    -DCONFIG_RUNTIME_CPU_DETECT=1 \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DCMAKE_CXX_STANDARD=11 \
    -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DCMAKE_CXX_EXTENSIONS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make
make install

echo '\n--------------------'
echo 'Building libavif'
echo '--------------------\n'
mkdir -p $BASEDIR/libavif
tar -xzf $SRCDIR/libavif-1.1.1.tar.gz -C $BASEDIR/libavif --strip-components 1
mkdir -p $BUILDDIR/libavif
cd $BUILDDIR/libavif
cmake $BASEDIR/libavif \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_C_FLAGS="-O3 -march=armv8-a+crc+crypto -mtune=apple-m1" \
    -DCMAKE_CXX_FLAGS="-O3 -march=armv8-a+crc+crypto -mtune=apple-m1" \
    -DAVIF_CODEC_AOM=SYSTEM \
    -DAVIF_BUILD_APPS=OFF \
    -DAVIF_ENABLE_NEON=ON \
    -DLIBYUV_LIBRARY=$PREFIX/lib/libyuv.a \
    -DLIBYUV_INCLUDE_DIR=$PREFIX/include \
    -DAOM_LIBRARY=$PREFIX/lib/libaom.a \
    -DAOM_INCLUDE_DIR=$PREFIX/include \
    -DJPEG_INCLUDE_DIR=$PREFIX/include \
    -DJPEG_LIBRARY=$PREFIX/lib/libjpeg.a \
    -DPNG_PNG_INCLUDE_DIR=$PREFIX/include \
    -DPNG_LIBRARY=$PREFIX/lib/libpng.a \
    -DCMAKE_PREFIX_PATH=$PREFIX \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX=$PREFIX
make
make install

rm -rf $BASEDIR/osx/$ARCH/bin
rm -f $BASEDIR/osx/$ARCH/**/*.cmake

# Since go modules don't currently download symlinked files
# (see https://github.com/golang/go/issues/39417)
# we replace symlinks with copies of the target.
# We use a `find -exec` with a separate file because POSIX sh
# is that much more limited than bash.
find "$PREFIX" -type l -exec "${BASEDIR}/copy-symlink-target.sh" {} \;
echo "Done!"

if [ -n "$CI" ]; then
  echo "CI detected, cleaning up build artifacts"
  rm -rf "$SRCDIR"
  rm -rf "$BUILDDIR"
fi
