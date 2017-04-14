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

#include "../perf_precomp.hpp"

#ifdef HAVE_CUDA

#include "opencv2/core/cuda.hpp"
#include "opencv2/ts/cuda_perf.hpp"

using namespace std;
using namespace testing;
using namespace perf;

//////////////////////////////////////////////////////////////////////
// SetTo

PERF_TEST_P(Sz_Depth_Cn, CUDA_GpuMat_SetTo,
            Combine(CUDA_TYPICAL_MAT_SIZES,
                    Values(CV_8U, CV_16U, CV_32F, CV_64F),
                    CUDA_CHANNELS_1_3_4))
{
    const cv::Size size = GET_PARAM(0);
    const int depth = GET_PARAM(1);
    const int channels = GET_PARAM(2);

    const int type = CV_MAKE_TYPE(depth, channels);

    const cv::Scalar val(1, 2, 3, 4);

    if (PERF_RUN_CUDA())
    {
        cv::cuda::GpuMat dst(size, type);

        TEST_CYCLE() dst.setTo(val);
    }
    else
    {
        cv::Mat dst(size, type);

        TEST_CYCLE() dst.setTo(val);
    }

    SANITY_CHECK_NOTHING();
}

//////////////////////////////////////////////////////////////////////
// SetToMasked

PERF_TEST_P(Sz_Depth_Cn, CUDA_GpuMat_SetToMasked,
            Combine(CUDA_TYPICAL_MAT_SIZES,
                    Values(CV_8U, CV_16U, CV_32F, CV_64F),
                    CUDA_CHANNELS_1_3_4))
{
    const cv::Size size = GET_PARAM(0);
    const int depth = GET_PARAM(1);
    const int channels = GET_PARAM(2);

    const int type = CV_MAKE_TYPE(depth, channels);

    cv::Mat src(size, type);
    cv::Mat mask(size, CV_8UC1);
    declare.in(src, mask, WARMUP_RNG);

    const cv::Scalar val(1, 2, 3, 4);

    if (PERF_RUN_CUDA())
    {
        cv::cuda::GpuMat dst(src);
        const cv::cuda::GpuMat d_mask(mask);

        TEST_CYCLE() dst.setTo(val, d_mask);
    }
    else
    {
        cv::Mat dst = src;

        TEST_CYCLE() dst.setTo(val, mask);
    }

    SANITY_CHECK_NOTHING();
}

//////////////////////////////////////////////////////////////////////
// CopyToMasked

PERF_TEST_P(Sz_Depth_Cn, CUDA_GpuMat_CopyToMasked,
            Combine(CUDA_TYPICAL_MAT_SIZES,
                    Values(CV_8U, CV_16U, CV_32F, CV_64F),
                    CUDA_CHANNELS_1_3_4))
{
    const cv::Size size = GET_PARAM(0);
    const int depth = GET_PARAM(1);
    const int channels = GET_PARAM(2);

    const int type = CV_MAKE_TYPE(depth, channels);

    cv::Mat src(size, type);
    cv::Mat mask(size, CV_8UC1);
    declare.in(src, mask, WARMUP_RNG);

    if (PERF_RUN_CUDA())
    {
        const cv::cuda::GpuMat d_src(src);
        const cv::cuda::GpuMat d_mask(mask);
        cv::cuda::GpuMat dst(d_src.size(), d_src.type(), cv::Scalar::all(0));

        TEST_CYCLE() d_src.copyTo(dst, d_mask);
    }
    else
    {
        cv::Mat dst(src.size(), src.type(), cv::Scalar::all(0));

        TEST_CYCLE() src.copyTo(dst, mask);
    }

    SANITY_CHECK_NOTHING();
}

//////////////////////////////////////////////////////////////////////
// ConvertTo

DEF_PARAM_TEST(Sz_2Depth, cv::Size, MatDepth, MatDepth);

PERF_TEST_P(Sz_2Depth, CUDA_GpuMat_ConvertTo,
            Combine(CUDA_TYPICAL_MAT_SIZES,
                    Values(CV_8U, CV_16U, CV_32F, CV_64F),
                    Values(CV_8U, CV_16U, CV_32F, CV_64F)))
{
    const cv::Size size = GET_PARAM(0);
    const int depth1 = GET_PARAM(1);
    const int depth2 = GET_PARAM(2);

    cv::Mat src(size, depth1);
    declare.in(src, WARMUP_RNG);

    const double a = 0.5;
    const double b = 1.0;

    if (PERF_RUN_CUDA())
    {
        const cv::cuda::GpuMat d_src(src);
        cv::cuda::GpuMat dst;

        TEST_CYCLE() d_src.convertTo(dst, depth2, a, b);
    }
    else
    {
        cv::Mat dst;

        TEST_CYCLE() src.convertTo(dst, depth2, a, b);
    }

    SANITY_CHECK_NOTHING();
}

#endif
