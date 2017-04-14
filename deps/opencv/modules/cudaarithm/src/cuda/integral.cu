/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
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

#include "opencv2/opencv_modules.hpp"

#ifndef HAVE_OPENCV_CUDEV

#error "opencv_cudev is required"

#else

#include "opencv2/cudaarithm.hpp"
#include "opencv2/cudev.hpp"
#include "opencv2/core/private.cuda.hpp"

using namespace cv;
using namespace cv::cuda;
using namespace cv::cudev;

////////////////////////////////////////////////////////////////////////
// integral

void cv::cuda::integral(InputArray _src, OutputArray _dst, Stream& stream)
{
    GpuMat src = getInputMat(_src, stream);

    CV_Assert( src.type() == CV_8UC1 );

    BufferPool pool(stream);
    GpuMat_<int> res(src.size(), pool.getAllocator());

    gridIntegral(globPtr<uchar>(src), res, stream);

    GpuMat dst = getOutputMat(_dst, src.rows + 1, src.cols + 1, CV_32SC1, stream);

    dst.setTo(Scalar::all(0), stream);

    GpuMat inner = dst(Rect(1, 1, src.cols, src.rows));
    res.copyTo(inner, stream);

    syncOutput(dst, _dst, stream);
}

//////////////////////////////////////////////////////////////////////////////
// sqrIntegral

void cv::cuda::sqrIntegral(InputArray _src, OutputArray _dst, Stream& stream)
{
    GpuMat src = getInputMat(_src, stream);

    CV_Assert( src.type() == CV_8UC1 );

    BufferPool pool(Stream::Null());
    GpuMat_<double> res(pool.getBuffer(src.size(), CV_64FC1));

    gridIntegral(sqr_(cvt_<int>(globPtr<uchar>(src))), res, stream);

    GpuMat dst = getOutputMat(_dst, src.rows + 1, src.cols + 1, CV_64FC1, stream);

    dst.setTo(Scalar::all(0), stream);

    GpuMat inner = dst(Rect(1, 1, src.cols, src.rows));
    res.copyTo(inner, stream);

    syncOutput(dst, _dst, stream);
}

#endif
