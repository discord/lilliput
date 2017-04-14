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
//    Niko Li, newlife20080214@gmail.com
//    Jia Haipeng, jiahaipeng95@gmail.com
//    Shengen Yan, yanshengen@gmail.com
//    Jiang Liyuan, lyuan001.good@163.com
//    Rock Li, Rock.Li@amd.com
//    Wu Zailong, bullet@yeah.net
//    Xu Pang, pangxu010@163.com
//    Sen Liu, swjtuls1987@126.com
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

enum
{
    noType = -1
};

/////////////////////////////////////////////////////////////////////////////////////////////////
// warpAffine  & warpPerspective

PARAM_TEST_CASE(WarpTestBase, MatType, Interpolation, bool, bool)
{
    int type, interpolation;
    Size dsize;
    bool useRoi, mapInverse;
    int depth;

    TEST_DECLARE_INPUT_PARAMETER(src);
    TEST_DECLARE_OUTPUT_PARAMETER(dst);

    virtual void SetUp()
    {
        type = GET_PARAM(0);
        interpolation = GET_PARAM(1);
        mapInverse = GET_PARAM(2);
        useRoi = GET_PARAM(3);
        depth = CV_MAT_DEPTH(type);

        if (mapInverse)
            interpolation |= WARP_INVERSE_MAP;
    }

    void random_roi()
    {
        dsize = randomSize(1, MAX_VALUE);

        Size roiSize = randomSize(1, MAX_VALUE);
        Border srcBorder = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(src, src_roi, roiSize, srcBorder, type, -MAX_VALUE, MAX_VALUE);

        Border dstBorder = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(dst, dst_roi, dsize, dstBorder, type, -MAX_VALUE, MAX_VALUE);

        UMAT_UPLOAD_INPUT_PARAMETER(src);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst);
    }

    void Near(double threshold = 0.0)
    {
        if (depth < CV_32F)
            EXPECT_MAT_N_DIFF(dst_roi, udst_roi, cvRound(dst_roi.total()*threshold));
        else
            OCL_EXPECT_MATS_NEAR_RELATIVE(dst, threshold);
    }
};

PARAM_TEST_CASE(WarpTest_cols4_Base, MatType, Interpolation, bool, bool)
{
    int type, interpolation;
    Size dsize;
    bool useRoi, mapInverse;
    int depth;

    TEST_DECLARE_INPUT_PARAMETER(src);
    TEST_DECLARE_OUTPUT_PARAMETER(dst);

    virtual void SetUp()
    {
        type = GET_PARAM(0);
        interpolation = GET_PARAM(1);
        mapInverse = GET_PARAM(2);
        useRoi = GET_PARAM(3);
        depth = CV_MAT_DEPTH(type);

        if (mapInverse)
            interpolation |= WARP_INVERSE_MAP;
    }

    void random_roi()
    {
        dsize = randomSize(1, MAX_VALUE);
        dsize.width = ((dsize.width >> 2) + 1) * 4;

        Size roiSize = randomSize(1, MAX_VALUE);
        Border srcBorder = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(src, src_roi, roiSize, srcBorder, type, -MAX_VALUE, MAX_VALUE);

        Border dstBorder = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(dst, dst_roi, dsize, dstBorder, type, -MAX_VALUE, MAX_VALUE);

        UMAT_UPLOAD_INPUT_PARAMETER(src);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst);
    }

    void Near(double threshold = 0.0)
    {
        if (depth < CV_32F)
            EXPECT_MAT_N_DIFF(dst_roi, udst_roi, cvRound(dst_roi.total()*threshold));
        else
            OCL_EXPECT_MATS_NEAR_RELATIVE(dst, threshold);
    }
};

/////warpAffine

typedef WarpTestBase WarpAffine;

/////warpAffine

typedef WarpTestBase WarpAffine;

OCL_TEST_P(WarpAffine, Mat)
{
    for (int j = 0; j < test_loop_times; j++)
    {
        double eps = depth < CV_32F ? 0.04 : 0.06;
        random_roi();

        Mat M = getRotationMatrix2D(Point2f(src_roi.cols / 2.0f, src_roi.rows / 2.0f),
            rng.uniform(-180.f, 180.f), rng.uniform(0.4f, 2.0f));

        OCL_OFF(cv::warpAffine(src_roi, dst_roi, M, dsize, interpolation));
        OCL_ON(cv::warpAffine(usrc_roi, udst_roi, M, dsize, interpolation));

        Near(eps);
    }
}

typedef WarpTest_cols4_Base WarpAffine_cols4;

OCL_TEST_P(WarpAffine_cols4, Mat)
{
    for (int j = 0; j < test_loop_times; j++)
    {
        double eps = depth < CV_32F ? 0.04 : 0.06;
        random_roi();

        Mat M = getRotationMatrix2D(Point2f(src_roi.cols / 2.0f, src_roi.rows / 2.0f),
            rng.uniform(-180.f, 180.f), rng.uniform(0.4f, 2.0f));

        OCL_OFF(cv::warpAffine(src_roi, dst_roi, M, dsize, interpolation));
        OCL_ON(cv::warpAffine(usrc_roi, udst_roi, M, dsize, interpolation));

        Near(eps);
    }
}

//// warpPerspective

typedef WarpTestBase WarpPerspective;

OCL_TEST_P(WarpPerspective, Mat)
{
    for (int j = 0; j < test_loop_times; j++)
    {
        double eps = depth < CV_32F ? 0.03 : 0.06;
        random_roi();

        float cols = static_cast<float>(src_roi.cols), rows = static_cast<float>(src_roi.rows);
        float cols2 = cols / 2.0f, rows2 = rows / 2.0f;
        Point2f sp[] = { Point2f(0.0f, 0.0f), Point2f(cols, 0.0f), Point2f(0.0f, rows), Point2f(cols, rows) };
        Point2f dp[] = { Point2f(rng.uniform(0.0f, cols2), rng.uniform(0.0f, rows2)),
            Point2f(rng.uniform(cols2, cols), rng.uniform(0.0f, rows2)),
            Point2f(rng.uniform(0.0f, cols2), rng.uniform(rows2, rows)),
            Point2f(rng.uniform(cols2, cols), rng.uniform(rows2, rows)) };
        Mat M = getPerspectiveTransform(sp, dp);

        OCL_OFF(cv::warpPerspective(src_roi, dst_roi, M, dsize, interpolation));
        OCL_ON(cv::warpPerspective(usrc_roi, udst_roi, M, dsize, interpolation));

        Near(eps);
    }
}

typedef WarpTest_cols4_Base WarpPerspective_cols4;

OCL_TEST_P(WarpPerspective_cols4, Mat)
{
    for (int j = 0; j < test_loop_times; j++)
    {
        double eps = depth < CV_32F ? 0.03 : 0.06;
        random_roi();

        float cols = static_cast<float>(src_roi.cols), rows = static_cast<float>(src_roi.rows);
        float cols2 = cols / 2.0f, rows2 = rows / 2.0f;
        Point2f sp[] = { Point2f(0.0f, 0.0f), Point2f(cols, 0.0f), Point2f(0.0f, rows), Point2f(cols, rows) };
        Point2f dp[] = { Point2f(rng.uniform(0.0f, cols2), rng.uniform(0.0f, rows2)),
            Point2f(rng.uniform(cols2, cols), rng.uniform(0.0f, rows2)),
            Point2f(rng.uniform(0.0f, cols2), rng.uniform(rows2, rows)),
            Point2f(rng.uniform(cols2, cols), rng.uniform(rows2, rows)) };
        Mat M = getPerspectiveTransform(sp, dp);

        OCL_OFF(cv::warpPerspective(src_roi, dst_roi, M, dsize, interpolation));
        OCL_ON(cv::warpPerspective(usrc_roi, udst_roi, M, dsize, interpolation));

        Near(eps);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////
//// resize

PARAM_TEST_CASE(Resize, MatType, double, double, Interpolation, bool, int)
{
    int type, interpolation;
    int widthMultiple;
    double fx, fy;
    bool useRoi;

    TEST_DECLARE_INPUT_PARAMETER(src);
    TEST_DECLARE_OUTPUT_PARAMETER(dst);

    virtual void SetUp()
    {
        type = GET_PARAM(0);
        fx = GET_PARAM(1);
        fy = GET_PARAM(2);
        interpolation = GET_PARAM(3);
        useRoi = GET_PARAM(4);
        widthMultiple = GET_PARAM(5);
    }

    void random_roi()
    {
        CV_Assert(fx > 0 && fy > 0);

        Size srcRoiSize = randomSize(10, MAX_VALUE), dstRoiSize;
        // Make sure the width is a multiple of the requested value, and no more
        srcRoiSize.width += widthMultiple - 1 - (srcRoiSize.width - 1) % widthMultiple;
        dstRoiSize.width = cvRound(srcRoiSize.width * fx);
        dstRoiSize.height = cvRound(srcRoiSize.height * fy);

        if (dstRoiSize.area() == 0)
        {
            random_roi();
            return;
        }

        Border srcBorder = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(src, src_roi, srcRoiSize, srcBorder, type, -MAX_VALUE, MAX_VALUE);

        Border dstBorder = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(dst, dst_roi, dstRoiSize, dstBorder, type, -MAX_VALUE, MAX_VALUE);

        UMAT_UPLOAD_INPUT_PARAMETER(src);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst);
    }

    void Near(double threshold = 0.0)
    {
        OCL_EXPECT_MATS_NEAR(dst, threshold);
    }
};

OCL_TEST_P(Resize, Mat)
{
    for (int j = 0; j < test_loop_times; j++)
    {
        int depth = CV_MAT_DEPTH(type);
        double eps = depth <= CV_32S ? 1 : 5e-2;

        random_roi();

        OCL_OFF(cv::resize(src_roi, dst_roi, Size(), fx, fy, interpolation));
        OCL_ON(cv::resize(usrc_roi, udst_roi, Size(), fx, fy, interpolation));

        Near(eps);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////
// remap

PARAM_TEST_CASE(Remap, MatDepth, Channels, std::pair<MatType, MatType>, BorderType, bool)
{
    int srcType, map1Type, map2Type;
    int borderType;
    bool useRoi;

    Scalar val;

    TEST_DECLARE_INPUT_PARAMETER(src);
    TEST_DECLARE_INPUT_PARAMETER(map1);
    TEST_DECLARE_INPUT_PARAMETER(map2);
    TEST_DECLARE_OUTPUT_PARAMETER(dst);

    virtual void SetUp()
    {
        srcType = CV_MAKE_TYPE(GET_PARAM(0), GET_PARAM(1));
        map1Type = GET_PARAM(2).first;
        map2Type = GET_PARAM(2).second;
        borderType = GET_PARAM(3);
        useRoi = GET_PARAM(4);
    }

    void random_roi()
    {
        val = randomScalar(-MAX_VALUE, MAX_VALUE);
        Size srcROISize = randomSize(1, MAX_VALUE);
        Size dstROISize = randomSize(1, MAX_VALUE);

        Border srcBorder = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(src, src_roi, srcROISize, srcBorder, srcType, 5, 256);

        Border dstBorder = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(dst, dst_roi, dstROISize, dstBorder, srcType, -MAX_VALUE, MAX_VALUE);

        int mapMaxValue = MAX_VALUE << 2;
        Border map1Border = randomBorder(0, useRoi ? MAX_VALUE : 0);
        randomSubMat(map1, map1_roi, dstROISize, map1Border, map1Type, -mapMaxValue, mapMaxValue);

        Border map2Border = randomBorder(0, useRoi ? MAX_VALUE + 1 : 0);
        if (map2Type != noType)
        {
            int mapMinValue = -mapMaxValue;
            if (map2Type == CV_16UC1 || map2Type == CV_16SC1)
                mapMinValue = 0, mapMaxValue = INTER_TAB_SIZE2;
            randomSubMat(map2, map2_roi, dstROISize, map2Border, map2Type, mapMinValue, mapMaxValue);
        }

        UMAT_UPLOAD_INPUT_PARAMETER(src);
        UMAT_UPLOAD_INPUT_PARAMETER(map1);
        UMAT_UPLOAD_OUTPUT_PARAMETER(dst);
        if (noType != map2Type)
            UMAT_UPLOAD_INPUT_PARAMETER(map2);
    }

    void Near(double threshold = 0.0)
    {
        OCL_EXPECT_MATS_NEAR(dst, threshold);
    }
};

typedef Remap Remap_INTER_NEAREST;

OCL_TEST_P(Remap_INTER_NEAREST, Mat)
{
    for (int j = 0; j < test_loop_times; j++)
    {
        random_roi();

        OCL_OFF(cv::remap(src_roi, dst_roi, map1_roi, map2_roi, INTER_NEAREST, borderType, val));
        OCL_ON(cv::remap(usrc_roi, udst_roi, umap1_roi, umap2_roi, INTER_NEAREST, borderType, val));

        Near(1.0);
    }
}

typedef Remap Remap_INTER_LINEAR;

OCL_TEST_P(Remap_INTER_LINEAR, Mat)
{
    for (int j = 0; j < test_loop_times; j++)
    {
        random_roi();

        double eps = 2.0;
#ifdef ANDROID
        // TODO investigate accuracy
        if (cv::ocl::Device::getDefault().isNVidia())
            eps = 8.0;
#endif

        OCL_OFF(cv::remap(src_roi, dst_roi, map1_roi, map2_roi, INTER_LINEAR, borderType, val));
        OCL_ON(cv::remap(usrc_roi, udst_roi, umap1_roi, umap2_roi, INTER_LINEAR, borderType, val));

        Near(eps);
    }
}

/////////////////////////////////////////////////////////////////////////////////////

OCL_INSTANTIATE_TEST_CASE_P(ImgprocWarp, WarpAffine, Combine(
                            Values(CV_8UC1, CV_8UC3, CV_8UC4, CV_32FC1, CV_32FC3, CV_32FC4),
                            Values((Interpolation)INTER_NEAREST, (Interpolation)INTER_LINEAR, (Interpolation)INTER_CUBIC),
                            Bool(),
                            Bool()));

OCL_INSTANTIATE_TEST_CASE_P(ImgprocWarp, WarpAffine_cols4, Combine(
                            Values((MatType)CV_8UC1),
                            Values((Interpolation)INTER_NEAREST, (Interpolation)INTER_LINEAR, (Interpolation)INTER_CUBIC),
                            Bool(),
                            Bool()));

OCL_INSTANTIATE_TEST_CASE_P(ImgprocWarp, WarpPerspective, Combine(
                            Values(CV_8UC1, CV_8UC3, CV_8UC4, CV_32FC1, CV_32FC3, CV_32FC4),
                            Values((Interpolation)INTER_NEAREST, (Interpolation)INTER_LINEAR, (Interpolation)INTER_CUBIC),
                            Bool(),
                            Bool()));

OCL_INSTANTIATE_TEST_CASE_P(ImgprocWarp, WarpPerspective_cols4, Combine(
                            Values((MatType)CV_8UC1),
                            Values((Interpolation)INTER_NEAREST, (Interpolation)INTER_LINEAR, (Interpolation)INTER_CUBIC),
                            Bool(),
                            Bool()));

OCL_INSTANTIATE_TEST_CASE_P(ImgprocWarp, Resize, Combine(
                            Values(CV_8UC1, CV_8UC4, CV_16UC2, CV_32FC1, CV_32FC4),
                            Values(0.5, 1.5, 2.0, 0.2),
                            Values(0.5, 1.5, 2.0, 0.2),
                            Values((Interpolation)INTER_NEAREST, (Interpolation)INTER_LINEAR),
                            Bool(),
                            Values(1, 16)));

OCL_INSTANTIATE_TEST_CASE_P(ImgprocWarpResizeArea, Resize, Combine(
                            Values((MatType)CV_8UC1, CV_8UC4, CV_32FC1, CV_32FC4),
                            Values(0.7, 0.4, 0.5),
                            Values(0.3, 0.6, 0.5),
                            Values((Interpolation)INTER_AREA),
                            Bool(),
                            Values(1, 16)));

OCL_INSTANTIATE_TEST_CASE_P(ImgprocWarp, Remap_INTER_LINEAR, Combine(
                            Values(CV_8U, CV_16U, CV_32F),
                            Values(1, 3, 4),
                            Values(std::pair<MatType, MatType>((MatType)CV_32FC1, (MatType)CV_32FC1),
                                   std::pair<MatType, MatType>((MatType)CV_16SC2, (MatType)CV_16UC1),
                                   std::pair<MatType, MatType>((MatType)CV_32FC2, noType)),
                            Values((BorderType)BORDER_CONSTANT,
                                   (BorderType)BORDER_REPLICATE,
                                   (BorderType)BORDER_WRAP,
                                   (BorderType)BORDER_REFLECT,
                                   (BorderType)BORDER_REFLECT_101),
                            Bool()));

OCL_INSTANTIATE_TEST_CASE_P(ImgprocWarp, Remap_INTER_NEAREST, Combine(
                            Values(CV_8U, CV_16U, CV_32F),
                            Values(1, 3, 4),
                            Values(std::pair<MatType, MatType>((MatType)CV_32FC1, (MatType)CV_32FC1),
                                   std::pair<MatType, MatType>((MatType)CV_32FC2, noType),
                                   std::pair<MatType, MatType>((MatType)CV_16SC2, (MatType)CV_16UC1),
                                   std::pair<MatType, MatType>((MatType)CV_16SC2, noType)),
                            Values((BorderType)BORDER_CONSTANT,
                                   (BorderType)BORDER_REPLICATE,
                                   (BorderType)BORDER_WRAP,
                                   (BorderType)BORDER_REFLECT,
                                   (BorderType)BORDER_REFLECT_101),
                            Bool()));

} } // namespace cvtest::ocl

#endif // HAVE_OPENCL
