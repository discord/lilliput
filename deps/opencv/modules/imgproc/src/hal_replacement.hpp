/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                          License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Copyright (C) 2015, Itseez Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#ifndef OPENCV_IMGPROC_HAL_REPLACEMENT_HPP
#define OPENCV_IMGPROC_HAL_REPLACEMENT_HPP

#include "opencv2/core/hal/interface.h"
#include "opencv2/imgproc/hal/interface.h"

#if defined __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#elif defined _MSC_VER
#  pragma warning( push )
#  pragma warning( disable: 4100 )
#endif

//! @addtogroup imgproc_hal_interface
//! @note Define your functions to override default implementations:
//! @code
//! #undef hal_add8u
//! #define hal_add8u my_add8u
//! @endcode
//! @{

/**
@brief Dummy structure storing filtering context

Users can convert this pointer to any type they want. Initialisation and destruction should be made in Init and Free function implementations correspondingly.
Example:
@code{.cpp}
int my_hal_filterInit(cvhalFilter2D **context, ...) {
    context = static_cast<cvhalFilter2D*>(new MyFilterData());
    //... init
}

int my_hal_filterFree(cvhalFilter2D *context) {
    MyFilterData *c = static_cast<MyFilterData*>(context);
    delete c;
}
@endcode
 */
struct cvhalFilter2D {};

/**
   @brief hal_filterInit
   @param context double pointer to user-defined context
   @param kernel_data pointer to kernel data
   @param kernel_step kernel step
   @param kernel_type kernel type (CV_8U, ...)
   @param kernel_width kernel width
   @param kernel_height kernel height
   @param max_width max possible image width, can be used to allocate working buffers
   @param max_height max possible image height
   @param src_type source image type
   @param dst_type destination image type
   @param borderType border processing mode (CV_HAL_BORDER_REFLECT, ...)
   @param delta added to pixel values
   @param anchor_x relative X position of center point within the kernel
   @param anchor_y relative Y position of center point within the kernel
   @param allowSubmatrix indicates whether the submatrices will be allowed as source image
   @param allowInplace indicates whether the inplace operation will be possible
   @sa cv::filter2D, cv::hal::Filter2D
 */
inline int hal_ni_filterInit(cvhalFilter2D **context, uchar *kernel_data, size_t kernel_step, int kernel_type, int kernel_width, int kernel_height, int max_width, int max_height, int src_type, int dst_type, int borderType, double delta, int anchor_x, int anchor_y, bool allowSubmatrix, bool allowInplace) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }
/**
   @brief hal_filter
   @param context pointer to user-defined context
   @param src_data source image data
   @param src_step source image step
   @param dst_data destination image data
   @param dst_step destination image step
   @param width images width
   @param height images height
   @param full_width full width of source image (outside the ROI)
   @param full_height full height of source image (outside the ROI)
   @param offset_x source image ROI offset X
   @param offset_y source image ROI offset Y
   @sa cv::filter2D, cv::hal::Filter2D
 */
inline int hal_ni_filter(cvhalFilter2D *context, uchar *src_data, size_t src_step, uchar *dst_data, size_t dst_step, int width, int height, int full_width, int full_height, int offset_x, int offset_y) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }
/**
   @brief hal_filterFree
   @param context pointer to user-defined context
   @sa cv::filter2D, cv::hal::Filter2D
 */
inline int hal_ni_filterFree(cvhalFilter2D *context) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

//! @cond IGNORED
#define cv_hal_filterInit hal_ni_filterInit
#define cv_hal_filter hal_ni_filter
#define cv_hal_filterFree hal_ni_filterFree
//! @endcond

/**
   @brief hal_sepFilterInit
   @param context double pointer to user-defined context
   @param src_type source image type
   @param dst_type destination image type
   @param kernel_type kernels type
   @param kernelx_data pointer to x-kernel data
   @param kernelx_length x-kernel vector length
   @param kernely_data pointer to y-kernel data
   @param kernely_length y-kernel vector length
   @param anchor_x relative X position of center point within the kernel
   @param anchor_y relative Y position of center point within the kernel
   @param delta added to pixel values
   @param borderType border processing mode (CV_HAL_BORDER_REFLECT, ...)
   @sa cv::sepFilter2D, cv::hal::SepFilter2D
 */
inline int hal_ni_sepFilterInit(cvhalFilter2D **context, int src_type, int dst_type, int kernel_type, uchar *kernelx_data, int kernelx_length, uchar *kernely_data, int kernely_length, int anchor_x, int anchor_y, double delta, int borderType) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }
/**
   @brief hal_sepFilter
   @param context pointer to user-defined context
   @param src_data source image data
   @param src_step source image step
   @param dst_data destination image data
   @param dst_step destination image step
   @param width images width
   @param height images height
   @param full_width full width of source image (outside the ROI)
   @param full_height full height of source image (outside the ROI)
   @param offset_x source image ROI offset X
   @param offset_y source image ROI offset Y
   @sa cv::sepFilter2D, cv::hal::SepFilter2D
 */
inline int hal_ni_sepFilter(cvhalFilter2D *context, uchar *src_data, size_t src_step, uchar* dst_data, size_t dst_step, int width, int height, int full_width, int full_height, int offset_x, int offset_y) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }
/**
   @brief hal_sepFilterFree
   @param context pointer to user-defined context
   @sa cv::sepFilter2D, cv::hal::SepFilter2D
 */
inline int hal_ni_sepFilterFree(cvhalFilter2D *context) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

//! @cond IGNORED
#define cv_hal_sepFilterInit hal_ni_sepFilterInit
#define cv_hal_sepFilter hal_ni_sepFilter
#define cv_hal_sepFilterFree hal_ni_sepFilterFree
//! @endcond

/**
   @brief hal_morphInit
   @param context double pointer to user-defined context
   @param operation morphology operation CV_HAL_MORPH_ERODE or CV_HAL_MORPH_DILATE
   @param src_type source image type
   @param dst_type destination image type
   @param max_width max possible image width, can be used to allocate working buffers
   @param max_height max possible image height
   @param kernel_type kernel type (CV_8U, ...)
   @param kernel_data pointer to kernel data
   @param kernel_step kernel step
   @param kernel_width kernel width
   @param kernel_height kernel height
   @param anchor_x relative X position of center point within the kernel
   @param anchor_y relative Y position of center point within the kernel
   @param borderType border processing mode (CV_HAL_BORDER_REFLECT, ...)
   @param borderValue values to use for CV_HAL_BORDER_CONSTANT mode
   @param iterations number of iterations
   @param allowSubmatrix indicates whether the submatrices will be allowed as source image
   @param allowInplace indicates whether the inplace operation will be possible
   @sa cv::erode, cv::dilate, cv::morphologyEx, cv::hal::Morph
 */
inline int hal_ni_morphInit(cvhalFilter2D **context, int operation, int src_type, int dst_type, int max_width, int max_height, int kernel_type, uchar *kernel_data, size_t kernel_step, int kernel_width, int kernel_height, int anchor_x, int anchor_y, int borderType, const double borderValue[4], int iterations, bool allowSubmatrix, bool allowInplace) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }
/**
   @brief hal_morph
   @param context pointer to user-defined context
   @param src_data source image data
   @param src_step source image step
   @param dst_data destination image data
   @param dst_step destination image step
   @param width images width
   @param height images height
   @param src_full_width full width of source image (outside the ROI)
   @param src_full_height full height of source image (outside the ROI)
   @param src_roi_x source image ROI X offset
   @param src_roi_y source image ROI Y offset
   @param dst_full_width full width of destination image
   @param dst_full_height full height of destination image
   @param dst_roi_x destination image ROI X offset
   @param dst_roi_y destination image ROI Y offset
   @sa cv::erode, cv::dilate, cv::morphologyEx, cv::hal::Morph
 */
inline int hal_ni_morph(cvhalFilter2D *context, uchar *src_data, size_t src_step, uchar *dst_data, size_t dst_step, int width, int height, int src_full_width, int src_full_height, int src_roi_x, int src_roi_y, int dst_full_width, int dst_full_height, int dst_roi_x, int dst_roi_y) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }
/**
   @brief hal_morphFree
   @param context pointer to user-defined context
   @sa cv::erode, cv::dilate, cv::morphologyEx, cv::hal::Morph
 */
inline int hal_ni_morphFree(cvhalFilter2D *context) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

//! @cond IGNORED
#define cv_hal_morphInit hal_ni_morphInit
#define cv_hal_morph hal_ni_morph
#define cv_hal_morphFree hal_ni_morphFree
//! @endcond

/**
   @brief hal_resize
   @param src_type source and destination image type
   @param src_data source image data
   @param src_step source image step
   @param src_width source image width
   @param src_height source image height
   @param dst_data destination image data
   @param dst_step destination image step
   @param dst_width destination image width
   @param dst_height destination image height
   @param inv_scale_x inversed scale X coefficient
   @param inv_scale_y inversed scale Y coefficient
   @param interpolation interpolation mode (CV_HAL_INTER_NEAREST, ...)
   @sa cv::resize, cv::hal::resize
 */
inline int hal_ni_resize(int src_type, const uchar *src_data, size_t src_step, int src_width, int src_height, uchar *dst_data, size_t dst_step, int dst_width, int dst_height, double inv_scale_x, double inv_scale_y, int interpolation) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }
/**
   @brief hal_warpAffine
   @param src_type source and destination image type
   @param src_data source image data
   @param src_step source image step
   @param src_width source image width
   @param src_height source image height
   @param dst_data destination image data
   @param dst_step destination image step
   @param dst_width destination image width
   @param dst_height destination image height
   @param M 2x3 matrix with transform coefficients
   @param interpolation interpolation mode (CV_HAL_INTER_NEAREST, ...)
   @param borderType border processing mode (CV_HAL_BORDER_REFLECT, ...)
   @param borderValue values to use for CV_HAL_BORDER_CONSTANT mode
   @sa cv::warpAffine, cv::hal::warpAffine
 */
inline int hal_ni_warpAffine(int src_type, const uchar *src_data, size_t src_step, int src_width, int src_height, uchar *dst_data, size_t dst_step, int dst_width, int dst_height, const double M[6], int interpolation, int borderType, const double borderValue[4]) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }
/**
   @brief hal_warpPerspectve
   @param src_type source and destination image type
   @param src_data source image data
   @param src_step source image step
   @param src_width source image width
   @param src_height source image height
   @param dst_data destination image data
   @param dst_step destination image step
   @param dst_width destination image width
   @param dst_height destination image height
   @param M 3x3 matrix with transform coefficients
   @param interpolation interpolation mode (CV_HAL_INTER_NEAREST, ...)
   @param borderType border processing mode (CV_HAL_BORDER_REFLECT, ...)
   @param borderValue values to use for CV_HAL_BORDER_CONSTANT mode
   @sa cv::warpPerspective, cv::hal::warpPerspective
 */
inline int hal_ni_warpPerspectve(int src_type, const uchar *src_data, size_t src_step, int src_width, int src_height, uchar *dst_data, size_t dst_step, int dst_width, int dst_height, const double M[9], int interpolation, int borderType, const double borderValue[4]) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

//! @cond IGNORED
#define cv_hal_resize hal_ni_resize
#define cv_hal_warpAffine hal_ni_warpAffine
#define cv_hal_warpPerspective hal_ni_warpPerspectve
//! @endcond

/**
   @brief hal_cvtBGRtoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U, CV_16U, CV_32F)
   @param scn source image channels (3 or 4)
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R channels will be swapped (BGR->RGB or RGB->BGR)
   Convert between BGR, BGRA, RGB and RGBA image formats.
 */
inline int hal_ni_cvtBGRtoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int scn, int dcn, bool swapBlue) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGRtoBGR5x5
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param scn source image channels (3 or 4)
   @param swapBlue if set to true B and R source channels will be swapped (treat as RGB)
   @param greenBits number of bits for green channel (5 or 6)
   Convert from BGR, BGRA, RGB and RGBA to packed BGR or RGB (16 bits per pixel, 555 or 565).
   Support only CV_8U images (input 3 or 4 channels, output 2 channels).
 */
inline int hal_ni_cvtBGRtoBGR5x5(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int scn, bool swapBlue, int greenBits) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGR5x5toBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R destination channels will be swapped (write RGB)
   @param greenBits number of bits for green channel (5 or 6)
   Convert from packed BGR or RGB (16 bits per pixel, 555 or 565) to BGR, BGRA, RGB and RGBA.
   Support only CV_8U images (input 2 channels, output 3 or 4 channels).
 */
inline int hal_ni_cvtBGR5x5toBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int dcn, bool swapBlue, int greenBits) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGRtoGray
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U, CV_16U or CV_32F)
   @param scn source image channels (3 or 4)
   @param swapBlue if set to true B and R source channels will be swapped (treat as RGB)
   Convert from BGR, BGRA, RGB or RGBA to 1-channel gray.
 */
inline int hal_ni_cvtBGRtoGray(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int scn, bool swapBlue) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtGraytoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U, CV_16U or CV_32F)
   @param dcn destination image channels (3 or 4)
   Convert from 1-channel gray to BGR, RGB, RGBA or BGRA.
 */
inline int hal_ni_cvtGraytoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int dcn) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGR5x5toGray
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param greenBits number of bits for green channel (5 or 6)
   Convert from packed BGR (16 bits per pixel, 555 or 565) to 1-channel gray.
   Support only CV_8U images.
 */
inline int hal_ni_cvtBGR5x5toGray(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int greenBits) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtGraytoBGR5x5
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param greenBits number of bits for green channel (5 or 6)
   Convert from 1-channel gray to packed BGR (16 bits per pixel, 555 or 565).
   Support only CV_8U images.
 */
inline int hal_ni_cvtGraytoBGR5x5(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int greenBits) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGRtoYUV
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U, CV_16U or CV_32F)
   @param scn source image channels (3 or 4)
   @param swapBlue if set to true B and R source channels will be swapped (treat as RGB)
   @param isCbCr if set to true write output in YCbCr format
   Convert from BGR, RGB, BGRA or RGBA to YUV or YCbCr.
 */
inline int hal_ni_cvtBGRtoYUV(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int scn, bool swapBlue, bool isCbCr) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtYUVtoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U, CV_16U or CV_32F)
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R destination channels will be swapped (write RGB)
   @param isCbCr if set to true treat source as YCbCr
   Convert from YUV or YCbCr to BGR, RGB, BGRA or RGBA.
 */
inline int hal_ni_cvtYUVtoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int dcn, bool swapBlue, bool isCbCr) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGRtoXYZ
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U, CV_16U or CV_32F)
   @param scn source image channels (3 or 4)
   @param swapBlue if set to true B and R source channels will be swapped (treat as RGB)
   Convert from BGR, RGB, BGRA or RGBA to XYZ.
 */
inline int hal_ni_cvtBGRtoXYZ(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int scn, bool swapBlue) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtXYZtoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U, CV_16U or CV_32F)
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R destination channels will be swapped (write RGB)
   Convert from XYZ to BGR, RGB, BGRA or RGBA.
 */
inline int hal_ni_cvtXYZtoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int dcn, bool swapBlue) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGRtoHSV
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U or CV_32F)
   @param scn source image channels (3 or 4)
   @param swapBlue if set to true B and R source channels will be swapped (treat as RGB)
   @param isFullRange if set to true write hue in range 0-255 (0-360 for float) otherwise in range 0-180
   @param isHSV if set to true write HSV otherwise HSL
   Convert from BGR, RGB, BGRA or RGBA to HSV or HSL.
 */
inline int hal_ni_cvtBGRtoHSV(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int scn, bool swapBlue, bool isFullRange, bool isHSV) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtHSVtoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U or CV_32F)
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R destination channels will be swapped (write RGB)
   @param isFullRange if set to true read hue in range 0-255 (0-360 for float) otherwise in range 0-180
   @param isHSV if set to true treat source as HSV otherwise HSL
   Convert from HSV or HSL to BGR, RGB, BGRA or RGBA.
 */
inline int hal_ni_cvtHSVtoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int dcn, bool swapBlue, bool isFullRange, bool isHSV) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGRtoLab
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U or CV_32F)
   @param scn source image channels (3 or 4)
   @param swapBlue if set to true B and R source channels will be swapped (treat as RGB)
   @param isLab if set to true write Lab otherwise Luv
   @param srgb if set to true use sRGB gamma correction
   Convert from BGR, RGB, BGRA or RGBA to Lab or Luv.
 */
inline int hal_ni_cvtBGRtoLab(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int scn, bool swapBlue, bool isLab, bool srgb) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtLabtoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param depth image depth (one of CV_8U or CV_32F)
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R destination channels will be swapped (write RGB)
   @param isLab if set to true treat input as Lab otherwise Luv
   @param srgb if set to true use sRGB gamma correction
   Convert from Lab or Luv to BGR, RGB, BGRA or RGBA.
 */
inline int hal_ni_cvtLabtoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int depth, int dcn, bool swapBlue, bool isLab, bool srgb) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtTwoPlaneYUVtoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param dst_width,dst_height destination image size
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R destination channels will be swapped (write RGB)
   @param uIdx U-channel index in the interleaved U/V plane (0 or 1)
   Convert from YUV (YUV420sp (or NV12/NV21) - Y plane followed by interleaved U/V plane) to BGR, RGB, BGRA or RGBA.
   Only for CV_8U.
 */
inline int hal_ni_cvtTwoPlaneYUVtoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int dst_width, int dst_height, int dcn, bool swapBlue, int uIdx) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtThreePlaneYUVtoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param dst_width,dst_height destination image size
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R destination channels will be swapped (write RGB)
   @param uIdx U-channel plane index (0 or 1)
   Convert from YUV (YUV420p (or YV12/YV21) - Y plane followed by U and V planes) to BGR, RGB, BGRA or RGBA.
   Only for CV_8U.
 */
inline int hal_ni_cvtThreePlaneYUVtoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int dst_width, int dst_height, int dcn, bool swapBlue, int uIdx) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtBGRtoThreePlaneYUV
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param scn source image channels (3 or 4)
   @param swapBlue if set to true B and R source channels will be swapped (treat as RGB)
   @param uIdx U-channel plane index (0 or 1)
   Convert from BGR, RGB, BGRA or RGBA to YUV (YUV420p (or YV12/YV21) - Y plane followed by U and V planes).
   Only for CV_8U.
 */
inline int hal_ni_cvtBGRtoThreePlaneYUV(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int scn, bool swapBlue, int uIdx) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtOnePlaneYUVtoBGR
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   @param dcn destination image channels (3 or 4)
   @param swapBlue if set to true B and R destination channels will be swapped (write RGB)
   @param uIdx U-channel index (0 or 1)
   @param ycn Y-channel index (0 or 1)
   Convert from UYVY, YUY2 or YVYU to BGR, RGB, BGRA or RGBA.
   Only for CV_8U.
 */
inline int hal_ni_cvtOnePlaneYUVtoBGR(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, int dcn, bool swapBlue, int uIdx, int ycn) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }


/**
   @brief hal_cvtRGBAtoMultipliedRGBA
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   Convert from BGRA or RGBA to format with multiplied alpha channel.
   Only for CV_8U.
 */
inline int hal_ni_cvtRGBAtoMultipliedRGBA(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

/**
   @brief hal_cvtMultipliedRGBAtoRGBA
   @param src_data,src_step source image data and step
   @param dst_data,dst_step destination image data and step
   @param width,height image size
   Convert from format with multiplied alpha channel to BGRA or RGBA.
   Only for CV_8U.
 */
inline int hal_ni_cvtMultipliedRGBAtoRGBA(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

//! @cond IGNORED
#define cv_hal_cvtBGRtoBGR hal_ni_cvtBGRtoBGR
#define cv_hal_cvtBGRtoBGR5x5 hal_ni_cvtBGRtoBGR5x5
#define cv_hal_cvtBGR5x5toBGR hal_ni_cvtBGR5x5toBGR
#define cv_hal_cvtBGRtoGray hal_ni_cvtBGRtoGray
#define cv_hal_cvtGraytoBGR hal_ni_cvtGraytoBGR
#define cv_hal_cvtBGR5x5toGray hal_ni_cvtBGR5x5toGray
#define cv_hal_cvtGraytoBGR5x5 hal_ni_cvtGraytoBGR5x5
#define cv_hal_cvtBGRtoYUV hal_ni_cvtBGRtoYUV
#define cv_hal_cvtYUVtoBGR hal_ni_cvtYUVtoBGR
#define cv_hal_cvtBGRtoXYZ hal_ni_cvtBGRtoXYZ
#define cv_hal_cvtXYZtoBGR hal_ni_cvtXYZtoBGR
#define cv_hal_cvtBGRtoHSV hal_ni_cvtBGRtoHSV
#define cv_hal_cvtHSVtoBGR hal_ni_cvtHSVtoBGR
#define cv_hal_cvtBGRtoLab hal_ni_cvtBGRtoLab
#define cv_hal_cvtLabtoBGR hal_ni_cvtLabtoBGR
#define cv_hal_cvtTwoPlaneYUVtoBGR hal_ni_cvtTwoPlaneYUVtoBGR
#define cv_hal_cvtThreePlaneYUVtoBGR hal_ni_cvtThreePlaneYUVtoBGR
#define cv_hal_cvtBGRtoThreePlaneYUV hal_ni_cvtBGRtoThreePlaneYUV
#define cv_hal_cvtOnePlaneYUVtoBGR hal_ni_cvtOnePlaneYUVtoBGR
#define cv_hal_cvtRGBAtoMultipliedRGBA hal_ni_cvtRGBAtoMultipliedRGBA
#define cv_hal_cvtMultipliedRGBAtoRGBA hal_ni_cvtMultipliedRGBAtoRGBA
//! @endcond

/**
   @brief Calculate integral image
   @param depth,sdepth,sqdepth Depths of source image, sum image and square sum image
   @param src_data,src_step Source image
   @param sum_data,sum_step Sum image
   @param sqsum_data,sqsum_step Square sum image
   @param tilted_data,tilted_step Tilted sum image
   @param width,height Source image dimensions
   @param cn Number of channels
   @note Following combinations of image depths are used:
   Source | Sum | Square sum
   -------|-----|-----------
   CV_8U | CV_32S | CV_64F
   CV_8U | CV_32S | CV_32F
   CV_8U | CV_32S | CV_32S
   CV_8U | CV_32F | CV_64F
   CV_8U | CV_32F | CV_32F
   CV_8U | CV_64F | CV_64F
   CV_16U | CV_64F | CV_64F
   CV_16S | CV_64F | CV_64F
   CV_32F | CV_32F | CV_64F
   CV_32F | CV_32F | CV_32F
   CV_32F | CV_64F | CV_64F
   CV_64F | CV_64F | CV_64F
   @sa cv::integral
*/
inline int hal_ni_integral(int depth, int sdepth, int sqdepth, const uchar * src_data, size_t src_step, uchar * sum_data, size_t sum_step, uchar * sqsum_data, size_t sqsum_step, uchar * tilted_data, size_t tilted_step, int width, int height, int cn) { return CV_HAL_ERROR_NOT_IMPLEMENTED; }

//! @cond IGNORED
#define cv_hal_integral hal_ni_integral
//! @endcond

//! @}

#if defined __GNUC__
#  pragma GCC diagnostic pop
#elif defined _MSC_VER
#  pragma warning( pop )
#endif

#include "custom_hal.hpp"

//! @cond IGNORED
#define CALL_HAL_RET(name, fun, retval, ...) \
    int res = fun(__VA_ARGS__, &retval); \
    if (res == CV_HAL_ERROR_OK) \
        return retval; \
    else if (res != CV_HAL_ERROR_NOT_IMPLEMENTED) \
        CV_Error_(cv::Error::StsInternal, \
            ("HAL implementation " CVAUX_STR(name) " ==> " CVAUX_STR(fun) " returned %d (0x%08x)", res, res));


#define CALL_HAL(name, fun, ...) \
    int res = fun(__VA_ARGS__); \
    if (res == CV_HAL_ERROR_OK) \
        return; \
    else if (res != CV_HAL_ERROR_NOT_IMPLEMENTED) \
        CV_Error_(cv::Error::StsInternal, \
            ("HAL implementation " CVAUX_STR(name) " ==> " CVAUX_STR(fun) " returned %d (0x%08x)", res, res));
//! @endcond

#endif
