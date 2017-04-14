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

#if defined _MSC_VER && _MSC_VER >= 1200
# pragma warning (disable : 4408 4201 4100)
#endif

static std::string path;

namespace {

template <class T_in, class T_out>
void generateIntegralTests(NCVAutoTestLister &testLister,
                           NCVTestSourceProvider<T_in> &src,
                           Ncv32u maxWidth, Ncv32u maxHeight)
{
    for (Ncv32f _i=1.0; _i<maxWidth; _i*=1.2f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "LinIntImgW%dH%d", i, 2);
        testLister.add(new TestIntegralImage<T_in, T_out>(testName, src, i, 2));
    }
    for (Ncv32f _i=1.0; _i<maxHeight; _i*=1.2f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "LinIntImgW%dH%d", 2, i);
        testLister.add(new TestIntegralImage<T_in, T_out>(testName, src, 2, i));
    }

    testLister.add(new TestIntegralImage<T_in, T_out>("LinIntImg_VGA", src, 640, 480));
}

void generateSquaredIntegralTests(NCVAutoTestLister &testLister, NCVTestSourceProvider<Ncv8u> &src,
                                  Ncv32u maxWidth, Ncv32u maxHeight)
{
    for (Ncv32f _i=1.0; _i<maxWidth; _i*=1.2f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "SqIntImgW%dH%d", i, 32);
        testLister.add(new TestIntegralImageSquared(testName, src, i, 32));
    }
    for (Ncv32f _i=1.0; _i<maxHeight; _i*=1.2f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "SqIntImgW%dH%d", 32, i);
        testLister.add(new TestIntegralImageSquared(testName, src, 32, i));
    }

    testLister.add(new TestIntegralImageSquared("SqLinIntImg_VGA", src, 640, 480));
}

void generateRectStdDevTests(NCVAutoTestLister &testLister, NCVTestSourceProvider<Ncv8u> &src,
                             Ncv32u maxWidth, Ncv32u maxHeight)
{
    NcvRect32u rect(1,1,18,18);

    for (Ncv32f _i=32; _i<maxHeight/2 && _i < maxWidth/2; _i*=1.2f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "RectStdDevW%dH%d", i*2, i);
        testLister.add(new TestRectStdDev(testName, src, i*2, i, rect, 1, true));
        testLister.add(new TestRectStdDev(testName, src, i*2, i, rect, 1.5, false));
        testLister.add(new TestRectStdDev(testName, src, i-1, i*2-1, rect, 1, false));
        testLister.add(new TestRectStdDev(testName, src, i-1, i*2-1, rect, 2.5, true));
    }

    testLister.add(new TestRectStdDev("RectStdDev_VGA", src, 640, 480, rect, 1, true));
}

template <class T>
void generateResizeTests(NCVAutoTestLister &testLister, NCVTestSourceProvider<T> &src)
{
    for (Ncv32u i=2; i<10; ++i)
    {
        char testName[80];
        sprintf(testName, "TestResize_VGA_s%d", i);
        testLister.add(new TestResize<T>(testName, src, 640, 480, i, true));
        testLister.add(new TestResize<T>(testName, src, 640, 480, i, false));
    }

    for (Ncv32u i=2; i<10; ++i)
    {
        char testName[80];
        sprintf(testName, "TestResize_1080_s%d", i);
        testLister.add(new TestResize<T>(testName, src, 1920, 1080, i, true));
        testLister.add(new TestResize<T>(testName, src, 1920, 1080, i, false));
    }
}

void generateNPPSTVectorTests(NCVAutoTestLister &testLister, NCVTestSourceProvider<Ncv32u> &src, Ncv32u maxLength)
{
    //compaction
    for (Ncv32f _i=256.0; _i<maxLength; _i*=1.5f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "Compaction%d", i);
        testLister.add(new TestCompact(testName, src, i, 0xFFFFFFFF, 30));
    }
    for (Ncv32u i=1; i<260; i++)
    {
        char testName[80];
        sprintf(testName, "Compaction%d", i);
        testLister.add(new TestCompact(testName, src, i, 0xC001C0DE, 70));
        testLister.add(new TestCompact(testName, src, i, 0xC001C0DE, 0));
        testLister.add(new TestCompact(testName, src, i, 0xC001C0DE, 100));
    }
    for (Ncv32u i=256*256-10; i<256*256+10; i++)
    {
        char testName[80];
        sprintf(testName, "Compaction%d", i);
        testLister.add(new TestCompact(testName, src, i, 0xFFFFFFFF, 40));
    }
    for (Ncv32u i=256*256*256-2; i<256*256*256+2; i++)
    {
        char testName[80];
        sprintf(testName, "Compaction%d", i);
        testLister.add(new TestCompact(testName, src, i, 0x00000000, 2));
    }
}


template <class T>
void generateTransposeTests(NCVAutoTestLister &testLister, NCVTestSourceProvider<T> &src)
{
    for (int i=2; i<64; i+=4)
    {
        for (int j=2; j<64; j+=4)
        {
            char testName[80];
            sprintf(testName, "TestTranspose_%dx%d", i, j);
            testLister.add(new TestTranspose<T>(testName, src, i, j));
        }
    }

    for (int i=1; i<128; i+=1)
    {
        for (int j=1; j<2; j+=1)
        {
            char testName[80];
            sprintf(testName, "TestTranspose_%dx%d", i, j);
            testLister.add(new TestTranspose<T>(testName, src, i, j));
        }
    }

    testLister.add(new TestTranspose<T>("TestTranspose_VGA", src, 640, 480));
    testLister.add(new TestTranspose<T>("TestTranspose_HD1080", src, 1920, 1080));

    //regression tests
    testLister.add(new TestTranspose<T>("TestTranspose_reg_0", src, 1072, 375));
}

template <class T>
void generateDrawRectsTests(NCVAutoTestLister &testLister,
                            NCVTestSourceProvider<T> &src,
                            NCVTestSourceProvider<Ncv32u> &src32u,
                            Ncv32u maxWidth, Ncv32u maxHeight)
{
    for (Ncv32f _i=16.0; _i<maxWidth; _i*=1.1f)
    {
        Ncv32u i = (Ncv32u)_i;
        Ncv32u j = maxHeight * i / maxWidth;
        if (!j) continue;
        char testName[80];
        sprintf(testName, "DrawRectsW%dH%d", i, j);

        if (sizeof(T) == sizeof(Ncv32u))
        {
            testLister.add(new TestDrawRects<T>(testName, src, src32u, i, j, i*j/1000+1, (T)0xFFFFFFFF));
        }
        else if (sizeof(T) == sizeof(Ncv8u))
        {
            testLister.add(new TestDrawRects<T>(testName, src, src32u, i, j, i*j/1000+1, (T)0xFF));
        }
        else
        {
            ncvAssertPrintCheck(false, "Attempted to instantiate non-existing DrawRects test suite");
        }
    }

    //test VGA
    testLister.add(new TestDrawRects<T>("DrawRects_VGA", src, src32u, 640, 480, 640*480/1000, (T)0xFF));
}

void generateVectorTests(NCVAutoTestLister &testLister, NCVTestSourceProvider<Ncv32u> &src, Ncv32u maxLength)
{
    //growth
    for (Ncv32f _i=10.0; _i<maxLength; _i*=1.5f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "VectorGrow%d", i);
        testLister.add(new TestHypothesesGrow(testName, src, 20, 20, 2.2f, i, i/2, i, i/4));
        testLister.add(new TestHypothesesGrow(testName, src, 10, 42, 1.2f, i, i, i, 0));
    }
    testLister.add(new TestHypothesesGrow("VectorGrow01b", src, 10, 42, 1.2f, 10, 0, 10, 1));
    testLister.add(new TestHypothesesGrow("VectorGrow11b", src, 10, 42, 1.2f, 10, 1, 10, 1));
    testLister.add(new TestHypothesesGrow("VectorGrow10b", src, 10, 42, 1.2f, 10, 1, 10, 0));
    testLister.add(new TestHypothesesGrow("VectorGrow00b", src, 10, 42, 1.2f, 10, 0, 10, 0));
}

void generateHypothesesFiltrationTests(NCVAutoTestLister &testLister, NCVTestSourceProvider<Ncv32u> &src, Ncv32u maxLength)
{
    for (Ncv32f _i=1.0; _i<maxLength; _i*=1.1f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "HypFilter%d", i);
        testLister.add(new TestHypothesesFilter(testName, src, i, 3, 0.2f));
        testLister.add(new TestHypothesesFilter(testName, src, i, 0, 0.2f));
        testLister.add(new TestHypothesesFilter(testName, src, i, 1, 0.1f));
    }
}


void generateHaarLoaderTests(NCVAutoTestLister &testLister)
{
    testLister.add(new TestHaarCascadeLoader("haarcascade_eye.xml", path + "haarcascade_eye.xml"));
    testLister.add(new TestHaarCascadeLoader("haarcascade_frontalface_alt.xml", path + "haarcascade_frontalface_alt.xml"));
    testLister.add(new TestHaarCascadeLoader("haarcascade_frontalface_alt2.xml", path + "haarcascade_frontalface_alt2.xml"));
    testLister.add(new TestHaarCascadeLoader("haarcascade_frontalface_alt_tree.xml", path + "haarcascade_frontalface_alt_tree.xml"));
    testLister.add(new TestHaarCascadeLoader("haarcascade_eye_tree_eyeglasses.xml", path + "haarcascade_eye_tree_eyeglasses.xml"));
}

void generateHaarApplicationTests(NCVAutoTestLister &testLister, NCVTestSourceProvider<Ncv8u> &src,
                                  Ncv32u maxWidth, Ncv32u maxHeight)
{
    (void)maxHeight;
    for (Ncv32u i=100; i<512; i+=41)
    {
        for (Ncv32u j=100; j<128; j+=25)
        {
            char testName[80];
            sprintf(testName, "HaarAppl%d_%d", i, j);
            testLister.add(new TestHaarCascadeApplication(testName, src, path + "haarcascade_frontalface_alt.xml", j, i));
        }
    }
    for (Ncv32f _i=20.0; _i<maxWidth; _i*=1.5f)
    {
        Ncv32u i = (Ncv32u)_i;
        char testName[80];
        sprintf(testName, "HaarAppl%d", i);
        testLister.add(new TestHaarCascadeApplication(testName, src, path + "haarcascade_frontalface_alt.xml", i, i));
    }
}

static void devNullOutput(const cv::String& msg)
{
    (void)msg;
}

}

bool nvidia_NPPST_Integral_Image(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path.c_str();
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerII("NPPST Integral Image", outputLevel);

    NCVTestSourceProvider<Ncv8u> testSrcRandom_8u(2010, 0, 255, 2048, 2048);
    NCVTestSourceProvider<Ncv32f> testSrcRandom_32f(2010, -1.0f, 1.0f, 2048, 2048);

    generateIntegralTests<Ncv8u, Ncv32u>(testListerII, testSrcRandom_8u, 2048, 2048);
    generateIntegralTests<Ncv32f, Ncv32f>(testListerII, testSrcRandom_32f, 2048, 2048);

    return testListerII.invoke();
}

bool nvidia_NPPST_Squared_Integral_Image(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerSII("NPPST Squared Integral Image", outputLevel);

    NCVTestSourceProvider<Ncv8u> testSrcRandom_8u(2010, 0, 255, 2048, 2048);

    generateSquaredIntegralTests(testListerSII, testSrcRandom_8u, 2048, 2048);

    return testListerSII.invoke();
}

bool nvidia_NPPST_RectStdDev(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerRStdDev("NPPST RectStdDev", outputLevel);

    NCVTestSourceProvider<Ncv8u> testSrcRandom_8u(2010, 0, 255, 2048, 2048);

    generateRectStdDevTests(testListerRStdDev, testSrcRandom_8u, 2048, 2048);

    return testListerRStdDev.invoke();
}

bool nvidia_NPPST_Resize(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerResize("NPPST Resize", outputLevel);

    NCVTestSourceProvider<Ncv32u> testSrcRandom_32u(2010, 0, 0xFFFFFFFF, 2048, 2048);
    NCVTestSourceProvider<Ncv64u> testSrcRandom_64u(2010, 0, (Ncv64u) -1, 2048, 2048);

    generateResizeTests(testListerResize, testSrcRandom_32u);
    generateResizeTests(testListerResize, testSrcRandom_64u);

    return testListerResize.invoke();
}

bool nvidia_NPPST_Vector_Operations(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerNPPSTVectorOperations("NPPST Vector Operations", outputLevel);

    NCVTestSourceProvider<Ncv32u> testSrcRandom_32u(2010, 0, 0xFFFFFFFF, 2048, 2048);

    generateNPPSTVectorTests(testListerNPPSTVectorOperations, testSrcRandom_32u, 2048*2048);

    return testListerNPPSTVectorOperations.invoke();
}

bool nvidia_NPPST_Transpose(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerTranspose("NPPST Transpose", outputLevel);

    NCVTestSourceProvider<Ncv32u> testSrcRandom_32u(2010, 0, 0xFFFFFFFF, 2048, 2048);
    NCVTestSourceProvider<Ncv64u> testSrcRandom_64u(2010, 0, (Ncv64u) -1, 2048, 2048);

    generateTransposeTests(testListerTranspose, testSrcRandom_32u);
    generateTransposeTests(testListerTranspose, testSrcRandom_64u);

    return testListerTranspose.invoke();
}

bool nvidia_NCV_Vector_Operations(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerVectorOperations("Vector Operations", outputLevel);

    NCVTestSourceProvider<Ncv32u> testSrcRandom_32u(2010, 0, 0xFFFFFFFF, 2048, 2048);

    generateVectorTests(testListerVectorOperations, testSrcRandom_32u, 2048*2048);

    return testListerVectorOperations.invoke();

}

bool nvidia_NCV_Haar_Cascade_Loader(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerHaarLoader("Haar Cascade Loader", outputLevel);

    generateHaarLoaderTests(testListerHaarLoader);

    return testListerHaarLoader.invoke();
}

bool nvidia_NCV_Haar_Cascade_Application(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerHaarAppl("Haar Cascade Application", outputLevel);

    NCVTestSourceProvider<Ncv8u> testSrcFacesVGA_8u(path + "group_1_640x480_VGA.pgm");

    generateHaarApplicationTests(testListerHaarAppl, testSrcFacesVGA_8u, 640, 480);

    return testListerHaarAppl.invoke();
}

bool nvidia_NCV_Hypotheses_Filtration(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerHypFiltration("Hypotheses Filtration", outputLevel);

    NCVTestSourceProvider<Ncv32u> testSrcRandom_32u(2010, 0, 0xFFFFFFFF, 2048, 2048);

    generateHypothesesFiltrationTests(testListerHypFiltration, testSrcRandom_32u, 512);

    return testListerHypFiltration.invoke();
}

bool nvidia_NCV_Visualization(const std::string& test_data_path, OutputLevel outputLevel)
{
    path = test_data_path;
    ncvSetDebugOutputHandler(devNullOutput);

    NCVAutoTestLister testListerVisualize("Visualization", outputLevel);

    NCVTestSourceProvider<Ncv8u> testSrcRandom_8u(2010, 0, 255, 2048, 2048);
    NCVTestSourceProvider<Ncv32u> testSrcRandom_32u(2010, 0, RAND_MAX, 2048, 2048);

    generateDrawRectsTests(testListerVisualize, testSrcRandom_8u, testSrcRandom_32u, 2048, 2048);
    generateDrawRectsTests(testListerVisualize, testSrcRandom_32u, testSrcRandom_32u, 2048, 2048);

    return testListerVisualize.invoke();
}
