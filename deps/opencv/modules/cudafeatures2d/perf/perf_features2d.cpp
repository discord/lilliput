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

#include "perf_precomp.hpp"

using namespace std;
using namespace testing;
using namespace perf;

//////////////////////////////////////////////////////////////////////
// FAST

DEF_PARAM_TEST(Image_Threshold_NonMaxSuppression, string, int, bool);

PERF_TEST_P(Image_Threshold_NonMaxSuppression, FAST,
            Combine(Values<string>("gpu/perf/aloe.png"),
                    Values(20),
                    Bool()))
{
    const cv::Mat img = readImage(GET_PARAM(0), cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(img.empty());

    const int threshold = GET_PARAM(1);
    const bool nonMaxSuppersion = GET_PARAM(2);

    if (PERF_RUN_CUDA())
    {
        cv::Ptr<cv::cuda::FastFeatureDetector> d_fast =
                cv::cuda::FastFeatureDetector::create(threshold, nonMaxSuppersion,
                                                      cv::FastFeatureDetector::TYPE_9_16,
                                                      0.5 * img.size().area());

        const cv::cuda::GpuMat d_img(img);
        cv::cuda::GpuMat d_keypoints;

        TEST_CYCLE() d_fast->detectAsync(d_img, d_keypoints);

        std::vector<cv::KeyPoint> gpu_keypoints;
        d_fast->convert(d_keypoints, gpu_keypoints);

        sortKeyPoints(gpu_keypoints);

        SANITY_CHECK_KEYPOINTS(gpu_keypoints);
    }
    else
    {
        std::vector<cv::KeyPoint> cpu_keypoints;

        TEST_CYCLE() cv::FAST(img, cpu_keypoints, threshold, nonMaxSuppersion);

        SANITY_CHECK_KEYPOINTS(cpu_keypoints);
    }
}

//////////////////////////////////////////////////////////////////////
// ORB

DEF_PARAM_TEST(Image_NFeatures, string, int);

PERF_TEST_P(Image_NFeatures, ORB,
            Combine(Values<string>("gpu/perf/aloe.png"),
                    Values(4000)))
{
    declare.time(300.0);

    const cv::Mat img = readImage(GET_PARAM(0), cv::IMREAD_GRAYSCALE);
    ASSERT_FALSE(img.empty());

    const int nFeatures = GET_PARAM(1);

    if (PERF_RUN_CUDA())
    {
        cv::Ptr<cv::cuda::ORB> d_orb = cv::cuda::ORB::create(nFeatures);

        const cv::cuda::GpuMat d_img(img);
        cv::cuda::GpuMat d_keypoints, d_descriptors;

        TEST_CYCLE() d_orb->detectAndComputeAsync(d_img, cv::noArray(), d_keypoints, d_descriptors);

        std::vector<cv::KeyPoint> gpu_keypoints;
        d_orb->convert(d_keypoints, gpu_keypoints);

        cv::Mat gpu_descriptors(d_descriptors);

        gpu_keypoints.resize(10);
        gpu_descriptors = gpu_descriptors.rowRange(0, 10);

        sortKeyPoints(gpu_keypoints, gpu_descriptors);

        SANITY_CHECK_KEYPOINTS(gpu_keypoints, 1e-4);
        SANITY_CHECK(gpu_descriptors);
    }
    else
    {
        cv::Ptr<cv::ORB> orb = cv::ORB::create(nFeatures);

        std::vector<cv::KeyPoint> cpu_keypoints;
        cv::Mat cpu_descriptors;

        TEST_CYCLE() orb->detectAndCompute(img, cv::noArray(), cpu_keypoints, cpu_descriptors);

        SANITY_CHECK_KEYPOINTS(cpu_keypoints);
        SANITY_CHECK(cpu_descriptors);
    }
}

//////////////////////////////////////////////////////////////////////
// BFMatch

DEF_PARAM_TEST(DescSize_Norm, int, NormType);

PERF_TEST_P(DescSize_Norm, BFMatch,
            Combine(Values(64, 128, 256),
                    Values(NormType(cv::NORM_L1), NormType(cv::NORM_L2), NormType(cv::NORM_HAMMING))))
{
    declare.time(20.0);

    const int desc_size = GET_PARAM(0);
    const int normType = GET_PARAM(1);

    const int type = normType == cv::NORM_HAMMING ? CV_8U : CV_32F;

    cv::Mat query(3000, desc_size, type);
    declare.in(query, WARMUP_RNG);

    cv::Mat train(3000, desc_size, type);
    declare.in(train, WARMUP_RNG);

    if (PERF_RUN_CUDA())
    {
        cv::Ptr<cv::cuda::DescriptorMatcher> d_matcher = cv::cuda::DescriptorMatcher::createBFMatcher(normType);

        const cv::cuda::GpuMat d_query(query);
        const cv::cuda::GpuMat d_train(train);
        cv::cuda::GpuMat d_matches;

        TEST_CYCLE() d_matcher->matchAsync(d_query, d_train, d_matches);

        std::vector<cv::DMatch> gpu_matches;
        d_matcher->matchConvert(d_matches, gpu_matches);

        SANITY_CHECK_MATCHES(gpu_matches);
    }
    else
    {
        cv::BFMatcher matcher(normType);

        std::vector<cv::DMatch> cpu_matches;

        TEST_CYCLE() matcher.match(query, train, cpu_matches);

        SANITY_CHECK_MATCHES(cpu_matches);
    }
}

//////////////////////////////////////////////////////////////////////
// BFKnnMatch

static void toOneRowMatches(const std::vector< std::vector<cv::DMatch> >& src, std::vector<cv::DMatch>& dst)
{
    dst.clear();
    for (size_t i = 0; i < src.size(); ++i)
        for (size_t j = 0; j < src[i].size(); ++j)
            dst.push_back(src[i][j]);
}

DEF_PARAM_TEST(DescSize_K_Norm, int, int, NormType);

PERF_TEST_P(DescSize_K_Norm, BFKnnMatch,
            Combine(Values(64, 128, 256),
                    Values(2, 3),
                    Values(NormType(cv::NORM_L1), NormType(cv::NORM_L2))))
{
    declare.time(30.0);

    const int desc_size = GET_PARAM(0);
    const int k = GET_PARAM(1);
    const int normType = GET_PARAM(2);

    const int type = normType == cv::NORM_HAMMING ? CV_8U : CV_32F;

    cv::Mat query(3000, desc_size, type);
    declare.in(query, WARMUP_RNG);

    cv::Mat train(3000, desc_size, type);
    declare.in(train, WARMUP_RNG);

    if (PERF_RUN_CUDA())
    {
        cv::Ptr<cv::cuda::DescriptorMatcher> d_matcher = cv::cuda::DescriptorMatcher::createBFMatcher(normType);

        const cv::cuda::GpuMat d_query(query);
        const cv::cuda::GpuMat d_train(train);
        cv::cuda::GpuMat d_matches;

        TEST_CYCLE() d_matcher->knnMatchAsync(d_query, d_train, d_matches, k);

        std::vector< std::vector<cv::DMatch> > matchesTbl;
        d_matcher->knnMatchConvert(d_matches, matchesTbl);

        std::vector<cv::DMatch> gpu_matches;
        toOneRowMatches(matchesTbl, gpu_matches);

        SANITY_CHECK_MATCHES(gpu_matches);
    }
    else
    {
        cv::BFMatcher matcher(normType);

        std::vector< std::vector<cv::DMatch> > matchesTbl;

        TEST_CYCLE() matcher.knnMatch(query, train, matchesTbl, k);

        std::vector<cv::DMatch> cpu_matches;
        toOneRowMatches(matchesTbl, cpu_matches);

        SANITY_CHECK_MATCHES(cpu_matches);
    }
}

//////////////////////////////////////////////////////////////////////
// BFRadiusMatch

PERF_TEST_P(DescSize_Norm, BFRadiusMatch,
            Combine(Values(64, 128, 256),
                    Values(NormType(cv::NORM_L1), NormType(cv::NORM_L2))))
{
    declare.time(30.0);

    const int desc_size = GET_PARAM(0);
    const int normType = GET_PARAM(1);

    const int type = normType == cv::NORM_HAMMING ? CV_8U : CV_32F;
    const float maxDistance = 10000;

    cv::Mat query(3000, desc_size, type);
    declare.in(query, WARMUP_RNG);

    cv::Mat train(3000, desc_size, type);
    declare.in(train, WARMUP_RNG);

    if (PERF_RUN_CUDA())
    {
        cv::Ptr<cv::cuda::DescriptorMatcher> d_matcher = cv::cuda::DescriptorMatcher::createBFMatcher(normType);

        const cv::cuda::GpuMat d_query(query);
        const cv::cuda::GpuMat d_train(train);
        cv::cuda::GpuMat d_matches;

        TEST_CYCLE() d_matcher->radiusMatchAsync(d_query, d_train, d_matches, maxDistance);

        std::vector< std::vector<cv::DMatch> > matchesTbl;
        d_matcher->radiusMatchConvert(d_matches, matchesTbl);

        std::vector<cv::DMatch> gpu_matches;
        toOneRowMatches(matchesTbl, gpu_matches);

        SANITY_CHECK_MATCHES(gpu_matches);
    }
    else
    {
        cv::BFMatcher matcher(normType);

        std::vector< std::vector<cv::DMatch> > matchesTbl;

        TEST_CYCLE() matcher.radiusMatch(query, train, matchesTbl, maxDistance);

        std::vector<cv::DMatch> cpu_matches;
        toOneRowMatches(matchesTbl, cpu_matches);

        SANITY_CHECK_MATCHES(cpu_matches);
    }
}
