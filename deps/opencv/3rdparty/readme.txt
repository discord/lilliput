This folder contains libraries and headers of a few very popular still image codecs
used by imgcodecs module.
The libraries and headers are preferably to build Win32 and Win64 versions of OpenCV.
On UNIX systems all the libraries are automatically detected by configure script.
In order to use these versions of libraries instead of system ones on UNIX systems you
should use BUILD_<library_name> CMake flags (for example, BUILD_PNG for the libpng library).

------------------------------------------------------------------------------------
libjpeg               The Independent JPEG Group's JPEG software.
                      Copyright (C) 1991-2012, Thomas G. Lane, Guido Vollbeding.
                      See IGJ home page http://www.ijg.org
                      for details and links to the source code

                      WITH_JPEG CMake option must be ON to add libjpeg support to imgcodecs.
------------------------------------------------------------------------------------
libpng                Portable Network Graphics library.
                      The license and copyright notes can be found in libpng/LICENSE.
                      See libpng home page http://www.libpng.org
                      for details and links to the source code

                      WITH_PNG CMake option must be ON to add libpng support to imgcodecs.
------------------------------------------------------------------------------------
libtiff               Tag Image File Format (TIFF) Software
                      Copyright (c) 1988-1997 Sam Leffler
                      Copyright (c) 1991-1997 Silicon Graphics, Inc.
                      See libtiff home page http://www.remotesensing.org/libtiff/
                      for details and links to the source code

                      WITH_TIFF CMake option must be ON to add libtiff & zlib support to imgcodecs.
------------------------------------------------------------------------------------
zlib                  General purpose LZ77 compression library
                      Copyright (C) 1995-2012 Jean-loup Gailly and Mark Adler.
                      See zlib home page http://www.zlib.net
                      for details and links to the source code
------------------------------------------------------------------------------------
jasper                JasPer is a collection of software
                      (i.e., a library and application programs) for the coding
                      and manipulation of images.  This software can handle image data in a
                      variety of formats.  One such format supported by JasPer is the JPEG-2000
                      format defined in ISO/IEC 15444-1.

                      Copyright (c) 1999-2000 Image Power, Inc.
                      Copyright (c) 1999-2000 The University of British Columbia
                      Copyright (c) 2001-2003 Michael David Adams

                      The JasPer license can be found in libjasper.
------------------------------------------------------------------------------------
openexr               OpenEXR is a high dynamic-range (HDR) image file format developed
                      by Industrial Light & Magic for use in computer imaging applications.

                      Copyright (c) 2006, Industrial Light & Magic, a division of Lucasfilm
                      Entertainment Company Ltd. Portions contributed and copyright held by
                      others as indicated. All rights reserved.

                      The project homepage: http://www.openexr.com
------------------------------------------------------------------------------------
ffmpeg                FFmpeg is a complete, cross-platform solution to record,
                      convert and stream audio and video. It includes libavcodec -
                      the leading audio/video codec library, and also libavformat, libavutils and
                      other helper libraries that are used by OpenCV (in videoio module) to
                      read and write video files.

                      Copyright (c) 2001 Fabrice Bellard

                      The project homepage: http://ffmpeg.org/.
                      
                      * On Linux/OSX we link user-installed ffmpeg (or ffmpeg fork libav).
                      * On Windows we use pre-built ffmpeg binaries,
                        see opencv/3rdparty/ffmpeg/readme.txt for details and licensing information
------------------------------------------------------------------------------------
