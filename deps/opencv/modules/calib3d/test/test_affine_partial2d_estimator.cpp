/*M///////////////////////////////////////////////////////////////////////////////////////
//
// By downloading, copying, installing or using the software you agree to this license.
// If you do not agree to this license, do not download, install,
// copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//                        (3-clause BSD License)
//
// Copyright (C) 2015-2016, OpenCV Foundation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * Neither the names of the copyright holders nor the names of the contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall copyright holders or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "test_precomp.hpp"

using namespace cv;
using namespace std;
using namespace testing;

#include <vector>
#include <numeric>

CV_ENUM(Method, RANSAC, LMEDS)
typedef TestWithParam<Method> EstimateAffinePartial2D;

static float rngIn(float from, float to) { return from + (to-from) * (float)theRNG(); }

// get random matrix of affine transformation limited to combinations of translation,
// rotation, and uniform scaling
static Mat rngPartialAffMat() {
    double theta = rngIn(0, (float)CV_PI*2.f);
    double scale = rngIn(0, 3);
    double tx = rngIn(-2, 2);
    double ty = rngIn(-2, 2);
    double aff[2*3] = { std::cos(theta) * scale, -std::sin(theta) * scale, tx,
                        std::sin(theta) * scale,  std::cos(theta) * scale, ty };
    return Mat(2, 3, CV_64F, aff).clone();
}

TEST_P(EstimateAffinePartial2D, test2Points)
{
    // try more transformations
    for (size_t i = 0; i < 500; ++i)
    {
        Mat aff = rngPartialAffMat();

        // setting points that are no in the same line
        Mat fpts(1, 2, CV_32FC2);
        Mat tpts(1, 2, CV_32FC2);

        fpts.at<Point2f>(0) = Point2f( rngIn(1,2), rngIn(5,6) );
        fpts.at<Point2f>(1) = Point2f( rngIn(3,4), rngIn(3,4) );

        transform(fpts, tpts, aff);

        vector<uchar> inliers;
        Mat aff_est = estimateAffinePartial2D(fpts, tpts, inliers, GetParam() /* method */);

        EXPECT_NEAR(0., cvtest::norm(aff_est, aff, NORM_INF), 1e-3);

        // all must be inliers
        EXPECT_EQ(countNonZero(inliers), 2);
    }
}

TEST_P(EstimateAffinePartial2D, testNPoints)
{
    // try more transformations
    for (size_t i = 0; i < 500; ++i)
    {
        Mat aff = rngPartialAffMat();

        const int method = GetParam();
        const int n = 100;
        int m;
        // LMEDS can't handle more than 50% outliers (by design)
        if (method == LMEDS)
            m = 3*n/5;
        else
            m = 2*n/5;
        const float shift_outl = 15.f;
        const float noise_level = 20.f;

        Mat fpts(1, n, CV_32FC2);
        Mat tpts(1, n, CV_32FC2);

        randu(fpts, 0., 100.);
        transform(fpts, tpts, aff);

        /* adding noise to some points */
        Mat outliers = tpts.colRange(m, n);
        outliers.reshape(1) += shift_outl;

        Mat noise (outliers.size(), outliers.type());
        randu(noise, 0., noise_level);
        outliers += noise;

        vector<uchar> inliers;
        Mat aff_est = estimateAffinePartial2D(fpts, tpts, inliers, method);

        EXPECT_FALSE(aff_est.empty());

        EXPECT_NEAR(0., cvtest::norm(aff_est, aff, NORM_INF), 1e-4);

        bool inliers_good = count(inliers.begin(), inliers.end(), 1) == m &&
            m == accumulate(inliers.begin(), inliers.begin() + m, 0);

        EXPECT_TRUE(inliers_good);
    }
}

INSTANTIATE_TEST_CASE_P(Calib3d, EstimateAffinePartial2D, Method::all());
