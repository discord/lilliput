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
// Copyright (C) 2009-2011, Willow Garage Inc., all rights reserved.
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

#include "precomp.hpp"
#include "opencv2/videostab/wobble_suppression.hpp"
#include "opencv2/videostab/ring_buffer.hpp"

#include "opencv2/core/private.cuda.hpp"

#ifdef HAVE_OPENCV_CUDAWARPING
#  include "opencv2/cudawarping.hpp"
#endif

#if defined(HAVE_OPENCV_CUDAWARPING)
    #if !defined HAVE_CUDA || defined(CUDA_DISABLER)
        namespace cv { namespace cuda {
            static void calcWobbleSuppressionMaps(int, int, int, Size, const Mat&, const Mat&, GpuMat&, GpuMat&) { throw_no_cuda(); }
        }}
    #else
        namespace cv { namespace cuda { namespace device { namespace globmotion {
            void calcWobbleSuppressionMaps(
                    int left, int idx, int right, int width, int height,
                    const float *ml, const float *mr, PtrStepSzf mapx, PtrStepSzf mapy);
        }}}}
        namespace cv { namespace cuda {
            static void calcWobbleSuppressionMaps(
                    int left, int idx, int right, Size size, const Mat &ml, const Mat &mr,
                    GpuMat &mapx, GpuMat &mapy)
            {
                CV_Assert(ml.size() == Size(3, 3) && ml.type() == CV_32F && ml.isContinuous());
                CV_Assert(mr.size() == Size(3, 3) && mr.type() == CV_32F && mr.isContinuous());

                mapx.create(size, CV_32F);
                mapy.create(size, CV_32F);

                cv::cuda::device::globmotion::calcWobbleSuppressionMaps(
                            left, idx, right, size.width, size.height,
                            ml.ptr<float>(), mr.ptr<float>(), mapx, mapy);
            }
        }}
    #endif
#endif

namespace cv
{
namespace videostab
{

WobbleSuppressorBase::WobbleSuppressorBase() : motions_(0), stabilizationMotions_(0)
{
    setMotionEstimator(makePtr<KeypointBasedMotionEstimator>(makePtr<MotionEstimatorRansacL2>(MM_HOMOGRAPHY)));
}


void NullWobbleSuppressor::suppress(int /*idx*/, const Mat &frame, Mat &result)
{
    result = frame;
}


void MoreAccurateMotionWobbleSuppressor::suppress(int idx, const Mat &frame, Mat &result)
{
    CV_Assert(motions_ && stabilizationMotions_);

    if (idx % period_ == 0)
    {
        result = frame;
        return;
    }

    int k1 = idx / period_ * period_;
    int k2 = std::min(k1 + period_, frameCount_ - 1);

    Mat S1 = (*stabilizationMotions_)[idx];

    Mat_<float> ML = S1 * getMotion(k1, idx, *motions2_) * getMotion(k1, idx, *motions_).inv() * S1.inv();
    Mat_<float> MR = S1 * getMotion(idx, k2, *motions2_).inv() * getMotion(idx, k2, *motions_) * S1.inv();

    mapx_.create(frame.size());
    mapy_.create(frame.size());

    float xl, yl, zl, wl;
    float xr, yr, zr, wr;

    for (int y = 0; y < frame.rows; ++y)
    {
        for (int x = 0; x < frame.cols; ++x)
        {
            xl = ML(0,0)*x + ML(0,1)*y + ML(0,2);
            yl = ML(1,0)*x + ML(1,1)*y + ML(1,2);
            zl = ML(2,0)*x + ML(2,1)*y + ML(2,2);
            xl /= zl; yl /= zl;
            wl = float(idx - k1);

            xr = MR(0,0)*x + MR(0,1)*y + MR(0,2);
            yr = MR(1,0)*x + MR(1,1)*y + MR(1,2);
            zr = MR(2,0)*x + MR(2,1)*y + MR(2,2);
            xr /= zr; yr /= zr;
            wr = float(k2 - idx);

            mapx_(y,x) = (wr * xl + wl * xr) / (wl + wr);
            mapy_(y,x) = (wr * yl + wl * yr) / (wl + wr);
        }
    }

    if (result.data == frame.data)
        result = Mat(frame.size(), frame.type());

    remap(frame, result, mapx_, mapy_, INTER_LINEAR, BORDER_REPLICATE);
}

#if defined(HAVE_OPENCV_CUDAWARPING)
void MoreAccurateMotionWobbleSuppressorGpu::suppress(int idx, const cuda::GpuMat &frame, cuda::GpuMat &result)
{
    CV_Assert(motions_ && stabilizationMotions_);

    if (idx % period_ == 0)
    {
        result = frame;
        return;
    }

    int k1 = idx / period_ * period_;
    int k2 = std::min(k1 + period_, frameCount_ - 1);

    Mat S1 = (*stabilizationMotions_)[idx];

    Mat ML = S1 * getMotion(k1, idx, *motions2_) * getMotion(k1, idx, *motions_).inv() * S1.inv();
    Mat MR = S1 * getMotion(idx, k2, *motions2_).inv() * getMotion(idx, k2, *motions_) * S1.inv();

    cuda::calcWobbleSuppressionMaps(k1, idx, k2, frame.size(), ML, MR, mapx_, mapy_);

    if (result.data == frame.data)
        result = cuda::GpuMat(frame.size(), frame.type());

    cuda::remap(frame, result, mapx_, mapy_, INTER_LINEAR, BORDER_REPLICATE);
}


void MoreAccurateMotionWobbleSuppressorGpu::suppress(int idx, const Mat &frame, Mat &result)
{
    frameDevice_.upload(frame);
    suppress(idx, frameDevice_, resultDevice_);
    resultDevice_.download(result);
}
#endif

} // namespace videostab
} // namespace cv
