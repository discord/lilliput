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
// Copyright (C) 2010-2012, Institute Of Software Chinese Academy Of Science, all rights reserved.
// Copyright (C) 2010-2012, Advanced Micro Devices, Inc., all rights reserved.
// Copyright (C) 2010-2012, Multicoreware, Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//    Jia Haipeng, jiahaipeng95@gmail.com
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

#include "../test_precomp.hpp"
#include "opencv2/ts/ocl_test.hpp"

#ifdef HAVE_OPENCL

namespace cvtest {
namespace ocl {

//////////////////////////////////////// Merge ///////////////////////////////////////////////

PARAM_TEST_CASE(Merge, MatDepth, int, bool)
{
    int depth, nsrc;
    bool use_roi;

    TEST_DECLARE_INPUT_PARAMETER(src1);
    TEST_DECLARE_INPUT_PARAMETER(src2);
    TEST_DECLARE_INPUT_PARAMETER(src3);
    TEST_DECLARE_INPUT_PARAMETER(src4);
    TEST_DECLARE_OUTPUT_PARAMETER(dst);

    std::vector<Mat> src_roi;
    std::vector<UMat> usrc_roi;

    virtual void SetUp()
    {
        depth = GET_PARAM(0);
        nsrc = GET_PARAM(1);
        use_roi = GET_PARAM(2);

        CV_Assert(nsrc >= 1 && nsrc <= 4);
    }

    int type()
    {
        return CV_MAKE_TYPE(depth, randomInt(1, 3));
    }

    void generateTestData()
    {
        Size roiSize = randomSize(1, MAX_VALUE);

        {
            Border src1Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(src1, src1_roi, roiSize, src1Border, type(), 2, 11);

            Border src2Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(src2, src2_roi, roiSize, src2Border, type(), -1540, 1740);

            Border src3Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(src3, src3_roi, roiSize, src3Border, type(), -1540, 1740);

            Border src4Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(src4, src4_roi, roiSize, src4Border, type(), -1540, 1740);
        }

        UMAT_UPLOAD_INPUT_PARAMETER(src1);
        UMAT_UPLOAD_INPUT_PARAMETER(src2);
        UMAT_UPLOAD_INPUT_PARAMETER(src3);
        UMAT_UPLOAD_INPUT_PARAMETER(src4);

        src_roi.clear(); usrc_roi.clear(); // for test_loop_times > 1
        src_roi.push_back(src1_roi), usrc_roi.push_back(usrc1_roi);
        if (nsrc >= 2)
            src_roi.push_back(src2_roi), usrc_roi.push_back(usrc2_roi);
        if (nsrc >= 3)
            src_roi.push_back(src3_roi), usrc_roi.push_back(usrc3_roi);
        if (nsrc >= 4)
            src_roi.push_back(src4_roi), usrc_roi.push_back(usrc4_roi);

        int dcn = 0;
        for (int i = 0; i < nsrc; ++i)
            dcn += src_roi[i].channels();

        Border dstBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(dst, dst_roi, roiSize, dstBorder, CV_MAKE_TYPE(depth, dcn), 5, 16);

        UMAT_UPLOAD_OUTPUT_PARAMETER(dst);
    }

    void Near(double threshold = 0.)
    {
        OCL_EXPECT_MATS_NEAR(dst, threshold);
    }
};

OCL_TEST_P(Merge, Accuracy)
{
    for(int j = 0; j < test_loop_times; j++)
    {
        generateTestData();

        OCL_OFF(cv::merge(src_roi, dst_roi));
        OCL_ON(cv::merge(usrc_roi, udst_roi));

        Near();
    }
}

//////////////////////////////////////// Split ///////////////////////////////////////////////

PARAM_TEST_CASE(Split, MatType, Channels, bool)
{
    int depth, cn;
    bool use_roi;

    TEST_DECLARE_INPUT_PARAMETER(src);
    TEST_DECLARE_OUTPUT_PARAMETER(dst1);
    TEST_DECLARE_OUTPUT_PARAMETER(dst2);
    TEST_DECLARE_OUTPUT_PARAMETER(dst3);
    TEST_DECLARE_OUTPUT_PARAMETER(dst4);

    std::vector<Mat> dst_roi, dst;
    std::vector<UMat> udst_roi, udst;

    virtual void SetUp()
    {
        depth = GET_PARAM(0);
        cn = GET_PARAM(1);
        use_roi = GET_PARAM(2);

        CV_Assert(cn >= 1 && cn <= 4);
    }

    void generateTestData()
    {
        Size roiSize = randomSize(1, MAX_VALUE);
        Border srcBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(src, src_roi, roiSize, srcBorder, CV_MAKE_TYPE(depth, cn), 5, 16);

        {
            Border dst1Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(dst1, dst1_roi, roiSize, dst1Border, depth, 2, 11);

            Border dst2Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(dst2, dst2_roi, roiSize, dst2Border, depth, -1540, 1740);

            Border dst3Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(dst3, dst3_roi, roiSize, dst3Border, depth, -1540, 1740);

            Border dst4Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(dst4, dst4_roi, roiSize, dst4Border, depth, -1540, 1740);
        }

        UMAT_UPLOAD_INPUT_PARAMETER(src);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst1);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst2);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst3);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst4);

        dst_roi.push_back(dst1_roi), udst_roi.push_back(udst1_roi),
                dst.push_back(dst1), udst.push_back(udst1);
        if (cn >= 2)
            dst_roi.push_back(dst2_roi), udst_roi.push_back(udst2_roi),
                    dst.push_back(dst2), udst.push_back(udst2);
        if (cn >= 3)
            dst_roi.push_back(dst3_roi), udst_roi.push_back(udst3_roi),
                    dst.push_back(dst3), udst.push_back(udst3);
        if (cn >= 4)
            dst_roi.push_back(dst4_roi), udst_roi.push_back(udst4_roi),
                    dst.push_back(dst4), udst.push_back(udst4);
    }
};

OCL_TEST_P(Split, Accuracy)
{
    for (int j = 0; j < test_loop_times; j++)
    {
        generateTestData();

        OCL_OFF(cv::split(src_roi, dst_roi));
        OCL_ON(cv::split(usrc_roi, udst_roi));

        for (int i = 0; i < cn; ++i)
        {
            EXPECT_MAT_NEAR(dst[i], udst[i], 0.0);
            EXPECT_MAT_NEAR(dst_roi[i], udst_roi[i], 0.0);
        }
    }
}

//////////////////////////////////////// MixChannels ///////////////////////////////////////////////

PARAM_TEST_CASE(MixChannels, MatType, bool)
{
    int depth;
    bool use_roi;

    TEST_DECLARE_INPUT_PARAMETER(src1);
    TEST_DECLARE_INPUT_PARAMETER(src2);
    TEST_DECLARE_INPUT_PARAMETER(src3);
    TEST_DECLARE_INPUT_PARAMETER(src4);
    TEST_DECLARE_OUTPUT_PARAMETER(dst1);
    TEST_DECLARE_OUTPUT_PARAMETER(dst2);
    TEST_DECLARE_OUTPUT_PARAMETER(dst3);
    TEST_DECLARE_OUTPUT_PARAMETER(dst4);

    std::vector<Mat> src_roi, dst_roi, dst;
    std::vector<UMat> usrc_roi, udst_roi, udst;
    std::vector<int> fromTo;

    virtual void SetUp()
    {
        depth = GET_PARAM(0);
        use_roi = GET_PARAM(1);
    }

    // generate number of channels and create type
    int type()
    {
        int cn = randomInt(1, 5);
        return CV_MAKE_TYPE(depth, cn);
    }

    void generateTestData()
    {
        src_roi.clear();
        dst_roi.clear();
        dst.clear();
        usrc_roi.clear();
        udst_roi.clear();
        udst.clear();
        fromTo.clear();

        Size roiSize = randomSize(1, MAX_VALUE);

        {
            Border src1Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(src1, src1_roi, roiSize, src1Border, type(), 2, 11);

            Border src2Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(src2, src2_roi, roiSize, src2Border, type(), -1540, 1740);

            Border src3Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(src3, src3_roi, roiSize, src3Border, type(), -1540, 1740);

            Border src4Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(src4, src4_roi, roiSize, src4Border, type(), -1540, 1740);
        }

        {
            Border dst1Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(dst1, dst1_roi, roiSize, dst1Border, type(), 2, 11);

            Border dst2Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(dst2, dst2_roi, roiSize, dst2Border, type(), -1540, 1740);

            Border dst3Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(dst3, dst3_roi, roiSize, dst3Border, type(), -1540, 1740);

            Border dst4Border = randomBorder(0, use_roi ? MAX_VALUE : 0);
            randomSubMat(dst4, dst4_roi, roiSize, dst4Border, type(), -1540, 1740);
        }

        UMAT_UPLOAD_INPUT_PARAMETER(src1);
        UMAT_UPLOAD_INPUT_PARAMETER(src2);
        UMAT_UPLOAD_INPUT_PARAMETER(src3);
        UMAT_UPLOAD_INPUT_PARAMETER(src4);

        UMAT_UPLOAD_OUTPUT_PARAMETER(dst1);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst2);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst3);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst4);

        int nsrc = randomInt(1, 5), ndst = randomInt(1, 5);

        src_roi.push_back(src1_roi), usrc_roi.push_back(usrc1_roi);
        if (nsrc >= 2)
            src_roi.push_back(src2_roi), usrc_roi.push_back(usrc2_roi);
        if (nsrc >= 3)
            src_roi.push_back(src3_roi), usrc_roi.push_back(usrc3_roi);
        if (nsrc >= 4)
            src_roi.push_back(src4_roi), usrc_roi.push_back(usrc4_roi);

        dst_roi.push_back(dst1_roi), udst_roi.push_back(udst1_roi),
                dst.push_back(dst1), udst.push_back(udst1);
        if (ndst >= 2)
            dst_roi.push_back(dst2_roi), udst_roi.push_back(udst2_roi),
                    dst.push_back(dst2), udst.push_back(udst2);
        if (ndst >= 3)
            dst_roi.push_back(dst3_roi), udst_roi.push_back(udst3_roi),
                    dst.push_back(dst3), udst.push_back(udst3);
        if (ndst >= 4)
            dst_roi.push_back(dst4_roi), udst_roi.push_back(udst4_roi),
                    dst.push_back(dst4), udst.push_back(udst4);

        int scntotal = 0, dcntotal = 0;
        for (int i = 0; i < nsrc; ++i)
            scntotal += src_roi[i].channels();
        for (int i = 0; i < ndst; ++i)
            dcntotal += dst_roi[i].channels();

        int npairs = randomInt(1, std::min(scntotal, dcntotal) + 1);
        fromTo.resize(npairs << 1);

        for (int i = 0; i < npairs; ++i)
        {
            fromTo[i<<1] = randomInt(0, scntotal);
            fromTo[(i<<1)+1] = randomInt(0, dcntotal);
        }
    }
};

OCL_TEST_P(MixChannels, Accuracy)
{
    for (int j = 0; j < test_loop_times + 10; j++)
    {
        generateTestData();

        OCL_OFF(cv::mixChannels(src_roi, dst_roi, fromTo));
        OCL_ON(cv::mixChannels(usrc_roi, udst_roi, fromTo));

        for (size_t i = 0, size = dst_roi.size(); i < size; ++i)
        {
            EXPECT_MAT_NEAR(dst[i], udst[i], 0.0);
            EXPECT_MAT_NEAR(dst_roi[i], udst_roi[i], 0.0);
        }
    }
}

//////////////////////////////////////// InsertChannel ///////////////////////////////////////////////

PARAM_TEST_CASE(InsertChannel, MatDepth, Channels, bool)
{
    int depth, cn, coi;
    bool use_roi;

    TEST_DECLARE_INPUT_PARAMETER(src);
    TEST_DECLARE_OUTPUT_PARAMETER(dst);

    virtual void SetUp()
    {
        depth = GET_PARAM(0);
        cn = GET_PARAM(1);
        use_roi = GET_PARAM(2);
    }

    void generateTestData()
    {
        Size roiSize = randomSize(1, MAX_VALUE);
        coi = randomInt(0, cn);

        Border srcBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(src, src_roi, roiSize, srcBorder, depth, 2, 11);

        Border dstBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(dst, dst_roi, roiSize, dstBorder, CV_MAKE_TYPE(depth, cn), 5, 16);

        UMAT_UPLOAD_INPUT_PARAMETER(src);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst);
    }
};

OCL_TEST_P(InsertChannel, Accuracy)
{
    for(int j = 0; j < test_loop_times; j++)
    {
        generateTestData();

        OCL_OFF(cv::insertChannel(src_roi, dst_roi, coi));
        OCL_ON(cv::insertChannel(usrc_roi, udst_roi, coi));

        OCL_EXPECT_MATS_NEAR(dst, 0);
    }
}

//////////////////////////////////////// ExtractChannel ///////////////////////////////////////////////

PARAM_TEST_CASE(ExtractChannel, MatDepth, Channels, bool)
{
    int depth, cn, coi;
    bool use_roi;

    TEST_DECLARE_INPUT_PARAMETER(src);
    TEST_DECLARE_OUTPUT_PARAMETER(dst);

    virtual void SetUp()
    {
        depth = GET_PARAM(0);
        cn = GET_PARAM(1);
        use_roi = GET_PARAM(2);
    }

    void generateTestData()
    {
        Size roiSize = randomSize(1, MAX_VALUE);
        coi = randomInt(0, cn);

        Border srcBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(src, src_roi, roiSize, srcBorder, CV_MAKE_TYPE(depth, cn), 2, 11);

        Border dstBorder = randomBorder(0, use_roi ? MAX_VALUE : 0);
        randomSubMat(dst, dst_roi, roiSize, dstBorder, depth, 5, 16);

        UMAT_UPLOAD_INPUT_PARAMETER(src);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst);
    }
};

OCL_TEST_P(ExtractChannel, Accuracy)
{
    for(int j = 0; j < test_loop_times; j++)
    {
        generateTestData();

        OCL_OFF(cv::extractChannel(src_roi, dst_roi, coi));
        OCL_ON(cv::extractChannel(usrc_roi, udst_roi, coi));

        OCL_EXPECT_MATS_NEAR(dst, 0);
    }
}

//////////////////////////////////////// Instantiation ///////////////////////////////////////////////

OCL_INSTANTIATE_TEST_CASE_P(Channels, Merge, Combine(OCL_ALL_DEPTHS, Values(1, 2, 3, 4), Bool()));
OCL_INSTANTIATE_TEST_CASE_P(Channels, Split, Combine(OCL_ALL_DEPTHS, OCL_ALL_CHANNELS, Bool()));
OCL_INSTANTIATE_TEST_CASE_P(Channels, MixChannels, Combine(OCL_ALL_DEPTHS, Bool()));
OCL_INSTANTIATE_TEST_CASE_P(Channels, InsertChannel, Combine(OCL_ALL_DEPTHS, OCL_ALL_CHANNELS, Bool()));
OCL_INSTANTIATE_TEST_CASE_P(Channels, ExtractChannel, Combine(OCL_ALL_DEPTHS, OCL_ALL_CHANNELS, Bool()));

} } // namespace cvtest::ocl

#endif // HAVE_OPENCL
