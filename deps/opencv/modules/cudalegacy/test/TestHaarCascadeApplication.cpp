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

#include "test_precomp.hpp"

namespace
{
    // http://www.christian-seiler.de/projekte/fpmath/
    class FpuControl
    {
    public:
        FpuControl();
        ~FpuControl();

    private:
    #if defined(__GNUC__) && !defined(__APPLE__) && !defined(__arm__) && !defined(__aarch64__) && !defined(__powerpc64__)
        fpu_control_t fpu_oldcw, fpu_cw;
    #elif defined(_WIN32) && !defined(_WIN64)
        unsigned int fpu_oldcw, fpu_cw;
    #endif
    };

    FpuControl::FpuControl()
    {
    #if defined(__GNUC__) && !defined(__APPLE__) && !defined(__arm__) && !defined(__aarch64__) && !defined(__powerpc64__)
        _FPU_GETCW(fpu_oldcw);
        fpu_cw = (fpu_oldcw & ~_FPU_EXTENDED & ~_FPU_DOUBLE & ~_FPU_SINGLE) | _FPU_SINGLE;
        _FPU_SETCW(fpu_cw);
    #elif defined(_WIN32) && !defined(_WIN64)
        _controlfp_s(&fpu_cw, 0, 0);
        fpu_oldcw = fpu_cw;
        _controlfp_s(&fpu_cw, _PC_24, _MCW_PC);
    #endif
    }

    FpuControl::~FpuControl()
    {
    #if defined(__GNUC__) && !defined(__APPLE__) && !defined(__arm__) && !defined(__aarch64__) && !defined(__powerpc64__)
        _FPU_SETCW(fpu_oldcw);
    #elif defined(_WIN32) && !defined(_WIN64)
        _controlfp_s(&fpu_cw, fpu_oldcw, _MCW_PC);
    #endif
    }
}

TestHaarCascadeApplication::TestHaarCascadeApplication(std::string testName_, NCVTestSourceProvider<Ncv8u> &src_,
                                                       std::string cascadeName_, Ncv32u width_, Ncv32u height_)
    :
    NCVTestProvider(testName_),
    src(src_),
    cascadeName(cascadeName_),
    width(width_),
    height(height_)
{
}


bool TestHaarCascadeApplication::toString(std::ofstream &strOut)
{
    strOut << "cascadeName=" << cascadeName << std::endl;
    strOut << "width=" << width << std::endl;
    strOut << "height=" << height << std::endl;
    return true;
}


bool TestHaarCascadeApplication::init()
{
    return true;
}

bool TestHaarCascadeApplication::process()
{
    NCVStatus ncvStat;
    bool rcode = false;

    Ncv32u numStages, numNodes, numFeatures;

    ncvStat = ncvHaarGetClassifierSize(this->cascadeName, numStages, numNodes, numFeatures);
    ncvAssertReturn(ncvStat == NCV_SUCCESS, false);

    NCVVectorAlloc<HaarStage64> h_HaarStages(*this->allocatorCPU.get(), numStages);
    ncvAssertReturn(h_HaarStages.isMemAllocated(), false);
    NCVVectorAlloc<HaarClassifierNode128> h_HaarNodes(*this->allocatorCPU.get(), numNodes);
    ncvAssertReturn(h_HaarNodes.isMemAllocated(), false);
    NCVVectorAlloc<HaarFeature64> h_HaarFeatures(*this->allocatorCPU.get(), numFeatures);
    ncvAssertReturn(h_HaarFeatures.isMemAllocated(), false);

    NCVVectorAlloc<HaarStage64> d_HaarStages(*this->allocatorGPU.get(), numStages);
    ncvAssertReturn(d_HaarStages.isMemAllocated(), false);
    NCVVectorAlloc<HaarClassifierNode128> d_HaarNodes(*this->allocatorGPU.get(), numNodes);
    ncvAssertReturn(d_HaarNodes.isMemAllocated(), false);
    NCVVectorAlloc<HaarFeature64> d_HaarFeatures(*this->allocatorGPU.get(), numFeatures);
    ncvAssertReturn(d_HaarFeatures.isMemAllocated(), false);

    HaarClassifierCascadeDescriptor haar;
    haar.ClassifierSize.width = haar.ClassifierSize.height = 1;
    haar.bNeedsTiltedII = false;
    haar.NumClassifierRootNodes = numNodes;
    haar.NumClassifierTotalNodes = numNodes;
    haar.NumFeatures = numFeatures;
    haar.NumStages = numStages;

    NCV_SET_SKIP_COND(this->allocatorGPU.get()->isCounting());
    NCV_SKIP_COND_BEGIN

    ncvStat = ncvHaarLoadFromFile_host(this->cascadeName, haar, h_HaarStages, h_HaarNodes, h_HaarFeatures);
    ncvAssertReturn(ncvStat == NCV_SUCCESS, false);

    ncvAssertReturn(NCV_SUCCESS == h_HaarStages.copySolid(d_HaarStages, 0), false);
    ncvAssertReturn(NCV_SUCCESS == h_HaarNodes.copySolid(d_HaarNodes, 0), false);
    ncvAssertReturn(NCV_SUCCESS == h_HaarFeatures.copySolid(d_HaarFeatures, 0), false);
    ncvAssertCUDAReturn(cudaStreamSynchronize(0), false);

    NCV_SKIP_COND_END

    NcvSize32s srcRoi, srcIIRoi, searchRoi;
    srcRoi.width = this->width;
    srcRoi.height = this->height;
    srcIIRoi.width = srcRoi.width + 1;
    srcIIRoi.height = srcRoi.height + 1;
    searchRoi.width = srcIIRoi.width - haar.ClassifierSize.width;
    searchRoi.height = srcIIRoi.height - haar.ClassifierSize.height;
    if (searchRoi.width <= 0 || searchRoi.height <= 0)
    {
        return false;
    }
    NcvSize32u searchRoiU(searchRoi.width, searchRoi.height);

    NCVMatrixAlloc<Ncv8u> d_img(*this->allocatorGPU.get(), this->width, this->height);
    ncvAssertReturn(d_img.isMemAllocated(), false);
    NCVMatrixAlloc<Ncv8u> h_img(*this->allocatorCPU.get(), this->width, this->height);
    ncvAssertReturn(h_img.isMemAllocated(), false);

    Ncv32u integralWidth = this->width + 1;
    Ncv32u integralHeight = this->height + 1;

    NCVMatrixAlloc<Ncv32u> d_integralImage(*this->allocatorGPU.get(), integralWidth, integralHeight);
    ncvAssertReturn(d_integralImage.isMemAllocated(), false);
    NCVMatrixAlloc<Ncv64u> d_sqIntegralImage(*this->allocatorGPU.get(), integralWidth, integralHeight);
    ncvAssertReturn(d_sqIntegralImage.isMemAllocated(), false);
    NCVMatrixAlloc<Ncv32u> h_integralImage(*this->allocatorCPU.get(), integralWidth, integralHeight);
    ncvAssertReturn(h_integralImage.isMemAllocated(), false);
    NCVMatrixAlloc<Ncv64u> h_sqIntegralImage(*this->allocatorCPU.get(), integralWidth, integralHeight);
    ncvAssertReturn(h_sqIntegralImage.isMemAllocated(), false);

    NCVMatrixAlloc<Ncv32f> d_rectStdDev(*this->allocatorGPU.get(), this->width, this->height);
    ncvAssertReturn(d_rectStdDev.isMemAllocated(), false);
    NCVMatrixAlloc<Ncv32u> d_pixelMask(*this->allocatorGPU.get(), this->width, this->height);
    ncvAssertReturn(d_pixelMask.isMemAllocated(), false);
    NCVMatrixAlloc<Ncv32f> h_rectStdDev(*this->allocatorCPU.get(), this->width, this->height);
    ncvAssertReturn(h_rectStdDev.isMemAllocated(), false);
    NCVMatrixAlloc<Ncv32u> h_pixelMask(*this->allocatorCPU.get(), this->width, this->height);
    ncvAssertReturn(h_pixelMask.isMemAllocated(), false);

    NCVVectorAlloc<NcvRect32u> d_hypotheses(*this->allocatorGPU.get(), this->width * this->height);
    ncvAssertReturn(d_hypotheses.isMemAllocated(), false);
    NCVVectorAlloc<NcvRect32u> h_hypotheses(*this->allocatorCPU.get(), this->width * this->height);
    ncvAssertReturn(h_hypotheses.isMemAllocated(), false);

    NCVStatus nppStat;
    Ncv32u szTmpBufIntegral, szTmpBufSqIntegral;
    nppStat = nppiStIntegralGetSize_8u32u(NcvSize32u(this->width, this->height), &szTmpBufIntegral, this->devProp);
    ncvAssertReturn(nppStat == NPPST_SUCCESS, false);
    nppStat = nppiStSqrIntegralGetSize_8u64u(NcvSize32u(this->width, this->height), &szTmpBufSqIntegral, this->devProp);
    ncvAssertReturn(nppStat == NPPST_SUCCESS, false);
    NCVVectorAlloc<Ncv8u> d_tmpIIbuf(*this->allocatorGPU.get(), std::max(szTmpBufIntegral, szTmpBufSqIntegral));
    ncvAssertReturn(d_tmpIIbuf.isMemAllocated(), false);

    Ncv32u detectionsOnThisScale_d = 0;
    Ncv32u detectionsOnThisScale_h = 0;

    NCV_SKIP_COND_BEGIN

    ncvAssertReturn(this->src.fill(h_img), false);
    ncvStat = h_img.copySolid(d_img, 0);
    ncvAssertReturn(ncvStat == NCV_SUCCESS, false);
    ncvAssertCUDAReturn(cudaStreamSynchronize(0), false);

    nppStat = nppiStIntegral_8u32u_C1R(d_img.ptr(), d_img.pitch(),
                                       d_integralImage.ptr(), d_integralImage.pitch(),
                                       NcvSize32u(d_img.width(), d_img.height()),
                                       d_tmpIIbuf.ptr(), szTmpBufIntegral, this->devProp);
    ncvAssertReturn(nppStat == NPPST_SUCCESS, false);

    nppStat = nppiStSqrIntegral_8u64u_C1R(d_img.ptr(), d_img.pitch(),
                                          d_sqIntegralImage.ptr(), d_sqIntegralImage.pitch(),
                                          NcvSize32u(d_img.width(), d_img.height()),
                                          d_tmpIIbuf.ptr(), szTmpBufSqIntegral, this->devProp);
    ncvAssertReturn(nppStat == NPPST_SUCCESS, false);

    const NcvRect32u rect(
        HAAR_STDDEV_BORDER,
        HAAR_STDDEV_BORDER,
        haar.ClassifierSize.width - 2*HAAR_STDDEV_BORDER,
        haar.ClassifierSize.height - 2*HAAR_STDDEV_BORDER);
    nppStat = nppiStRectStdDev_32f_C1R(
        d_integralImage.ptr(), d_integralImage.pitch(),
        d_sqIntegralImage.ptr(), d_sqIntegralImage.pitch(),
        d_rectStdDev.ptr(), d_rectStdDev.pitch(),
        NcvSize32u(searchRoi.width, searchRoi.height), rect,
        1.0f, true);
    ncvAssertReturn(nppStat == NPPST_SUCCESS, false);

    ncvStat = d_integralImage.copySolid(h_integralImage, 0);
    ncvAssertReturn(ncvStat == NCV_SUCCESS, false);
    ncvStat = d_rectStdDev.copySolid(h_rectStdDev, 0);
    ncvAssertReturn(ncvStat == NCV_SUCCESS, false);

    for (Ncv32u i=0; i<searchRoiU.height; i++)
    {
        for (Ncv32u j=0; j<h_pixelMask.stride(); j++)
        {
            if (j<searchRoiU.width)
            {
                h_pixelMask.ptr()[i*h_pixelMask.stride()+j] = (i << 16) | j;
            }
            else
            {
                h_pixelMask.ptr()[i*h_pixelMask.stride()+j] = OBJDET_MASK_ELEMENT_INVALID_32U;
            }
        }
    }
    ncvAssertReturn(cudaSuccess == cudaStreamSynchronize(0), false);

    {
        // calculations here
        FpuControl fpu;
        (void) fpu;

        ncvStat = ncvApplyHaarClassifierCascade_host(
            h_integralImage, h_rectStdDev, h_pixelMask,
            detectionsOnThisScale_h,
            haar, h_HaarStages, h_HaarNodes, h_HaarFeatures, false,
            searchRoiU, 1, 1.0f);
        ncvAssertReturn(ncvStat == NCV_SUCCESS, false);
    }

    NCV_SKIP_COND_END

    int devId;
    ncvAssertCUDAReturn(cudaGetDevice(&devId), false);
    cudaDeviceProp _devProp;
    ncvAssertCUDAReturn(cudaGetDeviceProperties(&_devProp, devId), false);

    ncvStat = ncvApplyHaarClassifierCascade_device(
        d_integralImage, d_rectStdDev, d_pixelMask,
        detectionsOnThisScale_d,
        haar, h_HaarStages, d_HaarStages, d_HaarNodes, d_HaarFeatures, false,
        searchRoiU, 1, 1.0f,
        *this->allocatorGPU.get(), *this->allocatorCPU.get(),
        _devProp, 0);
    ncvAssertReturn(ncvStat == NCV_SUCCESS, false);

    NCVMatrixAlloc<Ncv32u> h_pixelMask_d(*this->allocatorCPU.get(), this->width, this->height);
    ncvAssertReturn(h_pixelMask_d.isMemAllocated(), false);

    //bit-to-bit check
    bool bLoopVirgin = true;

    NCV_SKIP_COND_BEGIN

    ncvStat = d_pixelMask.copySolid(h_pixelMask_d, 0);
    ncvAssertReturn(ncvStat == NCV_SUCCESS, false);

    if (detectionsOnThisScale_d != detectionsOnThisScale_h)
    {
        bLoopVirgin = false;
    }
    else
    {
        std::sort(h_pixelMask_d.ptr(), h_pixelMask_d.ptr() + detectionsOnThisScale_d);
        for (Ncv32u i=0; i<detectionsOnThisScale_d && bLoopVirgin; i++)
        {
            if (h_pixelMask.ptr()[i] != h_pixelMask_d.ptr()[i])
            {
                bLoopVirgin = false;
            }
        }
    }

    NCV_SKIP_COND_END

    if (bLoopVirgin)
    {
        rcode = true;
    }

    return rcode;
}


bool TestHaarCascadeApplication::deinit()
{
    return true;
}
