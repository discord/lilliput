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

#ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wmissing-declarations"
#  if defined __clang__ || defined __APPLE__
#    pragma GCC diagnostic ignored "-Wmissing-prototypes"
#    pragma GCC diagnostic ignored "-Wextra"
#  endif
#endif

#ifndef __OPENCV_TEST_PRECOMP_HPP__
#define __OPENCV_TEST_PRECOMP_HPP__

#if defined(__GNUC__) && !defined(__APPLE__) && !defined(__arm__) && !defined(__aarch64__) && !defined(__powerpc64__)
    #include <fpu_control.h>
#endif

#include <cfloat>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <algorithm>
#include <fstream>

#include "opencv2/ts.hpp"
#include "opencv2/ts/cuda_test.hpp"

#include "opencv2/core/cuda.hpp"
#include "opencv2/cudalegacy.hpp"
#include "opencv2/highgui.hpp"

#include "opencv2/core/private.cuda.hpp"

#include "opencv2/opencv_modules.hpp"

#include "cvconfig.h"

#include "NCVTest.hpp"
#include "NCVAutoTestLister.hpp"
#include "NCVTestSourceProvider.hpp"

#include "TestIntegralImage.h"
#include "TestIntegralImageSquared.h"
#include "TestRectStdDev.h"
#include "TestResize.h"
#include "TestCompact.h"
#include "TestTranspose.h"
#include "TestDrawRects.h"
#include "TestHypothesesGrow.h"
#include "TestHypothesesFilter.h"
#include "TestHaarCascadeLoader.h"
#include "TestHaarCascadeApplication.h"

#include "main_test_nvidia.h"

#endif
