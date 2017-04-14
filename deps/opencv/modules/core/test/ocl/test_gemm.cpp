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
// Copyright (C) 2010-2012, Multicoreware, Inc., all rights reserved.
// Copyright (C) 2010-2012, Advanced Micro Devices, Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//    Peng Xiao, pengxiao@multicorewareinc.com
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
// This software is provided by the copyright holders and contributors as is and
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

#include "../test_precomp.hpp"
#include "opencv2/ts/ocl_test.hpp"

#ifdef HAVE_OPENCL

namespace cvtest {
namespace ocl {

////////////////////////////////////////////////////////////////////////////
// GEMM

PARAM_TEST_CASE(Gemm,
                MatType,
                bool, // GEMM_1_T
                bool, // GEMM_2_T
                bool, // GEMM_3_T
                bool // ROI
                )
{
    bool use_roi;
    int type, flags;
    bool atrans, btrans, ctrans;

    double alpha, beta;

    TEST_DECLARE_INPUT_PARAMETER(A);
    TEST_DECLARE_INPUT_PARAMETER(B);
    TEST_DECLARE_INPUT_PARAMETER(C);
    TEST_DECLARE_OUTPUT_PARAMETER(D);

    virtual void SetUp()
    {
        atrans = btrans = ctrans = false;

        type = GET_PARAM(0);
        use_roi = GET_PARAM(4);

        flags = 0;
        if (GET_PARAM(1))
            flags |= GEMM_1_T, atrans = true;
        if (GET_PARAM(2))
            flags |= GEMM_2_T, btrans = true;
        if (GET_PARAM(3))
            flags |= GEMM_3_T, ctrans = true;
    }

    void generateTestData()
    {
        // set minimum size to 20, since testing less sizes doesn't make sense
        Size ARoiSize = randomSize(20, MAX_VALUE);
        Border ABorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(A, A_roi, ARoiSize, ABorder, type, -11, 11);

        if (atrans)
            ARoiSize = Size(ARoiSize.height, ARoiSize.width);

        Size BRoiSize = randomSize(20, MAX_VALUE);
        if (btrans)
            BRoiSize.width = ARoiSize.width;
        else
            BRoiSize.height = ARoiSize.width;

        Border BBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(B, B_roi, BRoiSize, BBorder, type, -11, 11);

        if (btrans)
            BRoiSize = Size(BRoiSize.height, BRoiSize.width);

        Size DRoiSize = Size(BRoiSize.width, ARoiSize.height), CRoiSizeT(DRoiSize.height, DRoiSize.width);
        Border CBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(C, C_roi, ctrans ? CRoiSizeT : DRoiSize, CBorder, type, -11, 11);

        Border DBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(D, D_roi, DRoiSize, DBorder, type, -11, 11);

        alpha = randomDouble(-4, 4);
        beta = randomDouble(-4, 4);

        UMAT_UPLOAD_INPUT_PARAMETER(A);
        UMAT_UPLOAD_INPUT_PARAMETER(B);
        UMAT_UPLOAD_INPUT_PARAMETER(C);
        UMAT_UPLOAD_OUTPUT_PARAMETER(D);
    }
};

OCL_TEST_P(Gemm, Accuracy)
{
    for (int i = 0; i < test_loop_times; ++i)
    {
        generateTestData();

        OCL_OFF(cv::gemm(A_roi, B_roi, alpha, C_roi, beta, D_roi, flags));
        OCL_ON(cv::gemm(uA_roi, uB_roi, alpha, uC_roi, beta, uD_roi, flags));

        double eps = D_roi.size().area() * 1e-4;
        OCL_EXPECT_MATS_NEAR(D, eps);
    }
}

OCL_INSTANTIATE_TEST_CASE_P(Core, Gemm, ::testing::Combine(
                            testing::Values(CV_32FC1, CV_32FC2, CV_64FC1, CV_64FC2),
                            Bool(), Bool(), Bool(), Bool()));

} } // namespace cvtest::ocl

#endif // HAVE_OPENCL
