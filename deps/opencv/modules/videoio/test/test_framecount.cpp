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
#include "opencv2/videoio/videoio_c.h"
#include <stdio.h>

using namespace cv;
using namespace std;

class CV_FramecountTest: public cvtest::BaseTest
{
public:
    void run(int);
};

void CV_FramecountTest::run(int)
{
    const int time_sec = 5, fps = 25;

    const string ext[] = {"avi", "mov", "mp4"};

    const size_t n = sizeof(ext)/sizeof(ext[0]);

    const string src_dir = ts->get_data_path();

    ts->printf(cvtest::TS::LOG, "\n\nSource files directory: %s\n", (src_dir+"video/").c_str());

    Ptr<CvCapture> cap;

    for (size_t i = 0; i < n; ++i)
    {
        string file_path = src_dir+"video/big_buck_bunny."+ext[i];

        cap.reset(cvCreateFileCapture(file_path.c_str()));
        if (!cap)
        {
            ts->printf(cvtest::TS::LOG, "\nFile information (video %d): \n\nName: big_buck_bunny.%s\nFAILED\n\n", i+1, ext[i].c_str());
            ts->printf(cvtest::TS::LOG, "Error: cannot read source video file.\n");
            ts->set_failed_test_info(cvtest::TS::FAIL_INVALID_TEST_DATA);
            return;
        }

        //cvSetCaptureProperty(cap, CV_CAP_PROP_POS_FRAMES, 0);
        IplImage* frame; int FrameCount = 0;

        for(;;)
        {
            frame = cvQueryFrame(cap);
            if( !frame )
                break;
            FrameCount++;
        }

        int framecount = (int)cvGetCaptureProperty(cap, CAP_PROP_FRAME_COUNT);

        ts->printf(cvtest::TS::LOG, "\nFile information (video %d): \n"\
                   "\nName: big_buck_bunny.%s\nActual frame count: %d\n"\
                   "Frame count computed in the cycle of queries of frames: %d\n"\
                   "Frame count returned by cvGetCaptureProperty function: %d\n",
                   i+1, ext[i].c_str(), time_sec*fps, FrameCount, framecount);

        if( (FrameCount != cvRound(time_sec*fps) ||
             FrameCount != framecount) && ext[i] != "mpg" )
        {
            ts->printf(cvtest::TS::LOG, "FAILED\n");
            ts->printf(cvtest::TS::LOG, "\nError: actual frame count and returned frame count are not matched.\n");
            ts->set_failed_test_info(cvtest::TS::FAIL_INVALID_OUTPUT);
            return;
        }
    }
}
#if BUILD_WITH_VIDEO_INPUT_SUPPORT && defined HAVE_FFMPEG
TEST(Videoio_Video, framecount) {CV_FramecountTest test; test.safe_run();}
#endif
