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

#include "precomp.hpp"

using namespace cv;
using namespace cv::cuda;

#if !defined HAVE_CUDA || defined(CUDA_DISABLER)

void cv::cuda::calcOpticalFlowBM(const GpuMat&, const GpuMat&, Size, Size, Size, bool, GpuMat&, GpuMat&, GpuMat&, Stream&) { throw_no_cuda(); }

#else // HAVE_CUDA

namespace optflowbm
{
    void calc(PtrStepSzb prev, PtrStepSzb curr, PtrStepSzf velx, PtrStepSzf vely, int2 blockSize, int2 shiftSize, bool usePrevious,
              int maxX, int maxY, int acceptLevel, int escapeLevel, const short2* ss, int ssCount, cudaStream_t stream);
}

void cv::cuda::calcOpticalFlowBM(const GpuMat& prev, const GpuMat& curr, Size blockSize, Size shiftSize, Size maxRange, bool usePrevious, GpuMat& velx, GpuMat& vely, GpuMat& buf, Stream& st)
{
    CV_Assert( prev.type() == CV_8UC1 );
    CV_Assert( curr.size() == prev.size() && curr.type() == prev.type() );

    const Size velSize((prev.cols - blockSize.width + shiftSize.width) / shiftSize.width,
                       (prev.rows - blockSize.height + shiftSize.height) / shiftSize.height);

    velx.create(velSize, CV_32FC1);
    vely.create(velSize, CV_32FC1);

    // scanning scheme coordinates
    std::vector<short2> ss((2 * maxRange.width + 1) * (2 * maxRange.height + 1));
    int ssCount = 0;

    // Calculate scanning scheme
    const int minCount = std::min(maxRange.width, maxRange.height);

    // use spiral search pattern
    //
    //     9 10 11 12
    //     8  1  2 13
    //     7  *  3 14
    //     6  5  4 15
    //... 20 19 18 17
    //

    for (int i = 0; i < minCount; ++i)
    {
        // four cycles along sides
        int x = -i - 1, y = x;

        // upper side
        for (int j = -i; j <= i + 1; ++j, ++ssCount)
        {
            ss[ssCount].x = (short) ++x;
            ss[ssCount].y = (short) y;
        }

        // right side
        for (int j = -i; j <= i + 1; ++j, ++ssCount)
        {
            ss[ssCount].x = (short) x;
            ss[ssCount].y = (short) ++y;
        }

        // bottom side
        for (int j = -i; j <= i + 1; ++j, ++ssCount)
        {
            ss[ssCount].x = (short) --x;
            ss[ssCount].y = (short) y;
        }

        // left side
        for (int j = -i; j <= i + 1; ++j, ++ssCount)
        {
            ss[ssCount].x = (short) x;
            ss[ssCount].y = (short) --y;
        }
    }

    // the rest part
    if (maxRange.width < maxRange.height)
    {
        const int xleft = -minCount;

        // cycle by neighbor rings
        for (int i = minCount; i < maxRange.height; ++i)
        {
            // two cycles by x
            int y = -(i + 1);
            int x = xleft;

            // upper side
            for (int j = -maxRange.width; j <= maxRange.width; ++j, ++ssCount, ++x)
            {
                ss[ssCount].x = (short) x;
                ss[ssCount].y = (short) y;
            }

            x = xleft;
            y = -y;

            // bottom side
            for (int j = -maxRange.width; j <= maxRange.width; ++j, ++ssCount, ++x)
            {
                ss[ssCount].x = (short) x;
                ss[ssCount].y = (short) y;
            }
        }
    }
    else if (maxRange.width > maxRange.height)
    {
        const int yupper = -minCount;

        // cycle by neighbor rings
        for (int i = minCount; i < maxRange.width; ++i)
        {
            // two cycles by y
            int x = -(i + 1);
            int y = yupper;

            // left side
            for (int j = -maxRange.height; j <= maxRange.height; ++j, ++ssCount, ++y)
            {
                ss[ssCount].x = (short) x;
                ss[ssCount].y = (short) y;
            }

            y = yupper;
            x = -x;

            // right side
            for (int j = -maxRange.height; j <= maxRange.height; ++j, ++ssCount, ++y)
            {
                ss[ssCount].x = (short) x;
                ss[ssCount].y = (short) y;
            }
        }
    }

    const cudaStream_t stream = StreamAccessor::getStream(st);

    ensureSizeIsEnough(1, ssCount, CV_16SC2, buf);
    if (stream == 0)
        cudaSafeCall( cudaMemcpy(buf.data, &ss[0], ssCount * sizeof(short2), cudaMemcpyHostToDevice) );
    else
        cudaSafeCall( cudaMemcpyAsync(buf.data, &ss[0], ssCount * sizeof(short2), cudaMemcpyHostToDevice, stream) );

    const int maxX = prev.cols - blockSize.width;
    const int maxY = prev.rows - blockSize.height;

    const int SMALL_DIFF = 2;
    const int BIG_DIFF = 128;

    const int blSize = blockSize.area();
    const int acceptLevel = blSize * SMALL_DIFF;
    const int escapeLevel = blSize * BIG_DIFF;

    optflowbm::calc(prev, curr, velx, vely,
                    make_int2(blockSize.width, blockSize.height), make_int2(shiftSize.width, shiftSize.height), usePrevious,
                    maxX, maxY, acceptLevel, escapeLevel, buf.ptr<short2>(), ssCount, stream);
}

#endif // HAVE_CUDA
