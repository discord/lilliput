/*
 * Copyright (c) 2015, Piotr Dobrowolski dobrypd[at]gmail[dot]com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

const char * windowOriginal = "Captured preview";
const int FOCUS_STEP = 1024;
const int MAX_FOCUS_STEP = 32767;
const int FOCUS_DIRECTION_INFTY = 1;
const int DEFAULT_BREAK_LIMIT = 5;
const int DEFAULT_OUTPUT_FPS = 20;
const double epsylon = 0.0005; // compression, noice, etc.

struct Args_t
{
    string deviceName;
    string output;
    int fps;
    int minimumFocusStep;
    int breakLimit;
    bool measure;
    bool verbose;
} GlobalArgs;

struct FocusState
{
    int step;
    int direction;
    int minFocusStep;
    int lastDirectionChange;
    int stepToLastMax;
    double rate;
    double rateMax;
};

static ostream & operator<<(ostream & os, FocusState & state)
{
    return os << "RATE=" << state.rate << "\tSTEP="
            << state.step * state.direction << "\tLast change="
            << state.lastDirectionChange << "\tstepToLastMax="
            << state.stepToLastMax;
}

static FocusState createInitialState()
{
    FocusState state;
    state.step = FOCUS_STEP;
    state.direction = FOCUS_DIRECTION_INFTY;
    state.minFocusStep = 0;
    state.lastDirectionChange = 0;
    state.stepToLastMax = 0;
    state.rate = 0;
    state.rateMax = 0;
    return state;
}

static void focusDriveEnd(VideoCapture & cap, int direction)
{
    while (cap.set(CAP_PROP_ZOOM, (double) MAX_FOCUS_STEP * direction))
        ;
}

/**
 * Minimal focus step depends on lens
 * and I don't want to make any assumptions about it.
 */
static int findMinFocusStep(VideoCapture & cap, unsigned int startWith,
        int direction)
{
    int lStep, rStep;
    lStep = 0;
    rStep = startWith;

    focusDriveEnd(cap, direction * FOCUS_DIRECTION_INFTY);
    while (lStep < rStep)
    {
        int mStep = (lStep + rStep) / 2;
        cap.set(CAP_PROP_ZOOM, direction * FOCUS_DIRECTION_INFTY * FOCUS_STEP);
        if (cap.set(CAP_PROP_ZOOM, -direction * mStep))
        {
            rStep = mStep;
        }
        else
        {
            lStep = mStep + 1;
        }
    }
    cap.set(CAP_PROP_ZOOM, direction * FOCUS_DIRECTION_INFTY * MAX_FOCUS_STEP);
    if (GlobalArgs.verbose)
    {
        cout << "Found minimal focus step = " << lStep << endl;
    }
    return lStep;
}

/**
 * Rate frame from 0/blury/ to 1/sharp/.
 */
static double rateFrame(Mat & frame)
{
    unsigned long int sum = 0;
    unsigned long int size = frame.cols * frame.rows;
    Mat edges;
    cvtColor(frame, edges, CV_BGR2GRAY);
    GaussianBlur(edges, edges, Size(7, 7), 1.5, 1.5);
    Canny(edges, edges, 0, 30, 3);

    MatIterator_<uchar> it, end;
    for (it = edges.begin<uchar>(), end = edges.end<uchar>(); it != end; ++it)
    {
        sum += *it != 0;
    }

    return (double) sum / (double) size;
}

static int correctFocus(bool lastSucceeded, FocusState & state, double rate)
{
    if (GlobalArgs.verbose)
    {
        cout << "RATE=" << rate << endl;
    }
    state.lastDirectionChange++;
    double rateDelta = rate - state.rate;

    if (rate >= state.rateMax + epsylon)
    {
        // Update Max
        state.stepToLastMax = 0;
        state.rateMax = rate;
        // My local minimum is now on the other direction, that's why:
        state.lastDirectionChange = 0;
    }

    if (!lastSucceeded)
    {
        // Focus at limit or other problem, change the direction.
        state.direction *= -1;
        state.lastDirectionChange = 0;
        state.step /= 2;
    }
    else
    {
        if (rate < epsylon)
        { // It's hard to say anything
            state.step = FOCUS_STEP;
        }
        else if (rateDelta < -epsylon)
        { // Wrong direction ?
            state.direction *= -1;
            state.step = static_cast<int>(static_cast<double>(state.step) * 0.75);
            state.lastDirectionChange = 0;
        }
        else if ((rate + epsylon < state.rateMax)
                && ((state.lastDirectionChange > 3)
                        || ((state.step < (state.minFocusStep * 1.5))
                                && state.stepToLastMax > state.step)))
        { // I've done 3 steps (or I'm finishing) without improvement, go back to max.
            state.direction = state.stepToLastMax >= 0 ? 1 : -1;
            state.step = static_cast<int>(static_cast<double>(state.step) * 0.75);
            int stepToMax = abs(state.stepToLastMax);
            state.stepToLastMax = 0;
            state.lastDirectionChange = 0; // Like reset.
            state.rate = rate;
            return stepToMax;
        }
    }
    // Update state.
    state.rate = rate;
    state.stepToLastMax -= state.direction * state.step;
    return state.step;
}

static void showHelp(const char * pName, bool welcomeMsg)
{
    cout << "This program demonstrates usage of gPhoto2 VideoCapture.\n\n"
            "With OpenCV build without gPhoto2 library support it will "
            "do nothing special, just capture.\n\n"
            "Simple implementation of autofocus is based on edges detection.\n"
            "It was tested (this example) only with Nikon DSLR (Nikon D90).\n"
            "But shall work on all Nikon DSLRs, and with little effort with other devices.\n"
            "Visit http://www.gphoto.org/proj/libgphoto2/support.php\n"
            "to find supported devices (need Image Capture at least).\n"
            "Before run, set your camera autofocus ON.\n\n";

    if (!welcomeMsg)
    {
        cout << "usage " << pName << ": [OPTIONS] DEVICE_NAME\n\n"
                "OPTIONS:\n"
                "\t-h\t\treturns this help message,\n"
                "\t-o=<FILENAME>\tsave output video in file (MJPEG only),\n"
                "\t-f=FPS\t\tframes per second in output video,\n"
                "\t-m\t\tmeasure exposition\n"
                "\t\t\t(returns rates from closest focus to INTY\n"
                "\t\t\tfor every minimum step),\n"
                "\t-d=<INT>\t\tset minimum focus step,\n"
                "\t-v\t\tverbose mode.\n\n\n"
                "DEVICE_NAME\t\tis your digital camera model substring.\n\n\n"
                "On runtime you can use keys to control:\n";
    }
    else
    {
        cout << "Actions:\n";
    }

    cout << "\tk:\t- focus out,\n"
            "\tj:\t- focus in,\n"
            "\t,:\t- focus to the closest point,\n"
            "\t.:\t- focus to infinity,\n"
            "\tr:\t- reset autofocus state,\n"
            "\tf:\t- switch autofocus on/off,\n"
            "\tq:\t- quit.\n";
}

static bool parseArguments(int argc, char ** argv)
{
    cv::CommandLineParser parser(argc, argv, "{h help ||}{o||}{f||}{m||}{d|0|}{v||}{@device|Nikon|}");
    if (parser.has("help"))
        return false;
    GlobalArgs.breakLimit = DEFAULT_BREAK_LIMIT;
    if (parser.has("o"))
        GlobalArgs.output = parser.get<string>("o");
    else
        GlobalArgs.output = "";
    if (parser.has("f"))
        GlobalArgs.fps = parser.get<int>("f");
    else
        GlobalArgs.fps = DEFAULT_OUTPUT_FPS;
    GlobalArgs.measure = parser.has("m");
    GlobalArgs.verbose = parser.has("v");
    GlobalArgs.minimumFocusStep = parser.get<int>("d");
    GlobalArgs.deviceName = parser.get<string>("@device");
    if (!parser.check())
    {
        parser.printErrors();
        return false;
    }
    if (GlobalArgs.fps < 0)
    {
        cerr << "Invalid fps argument." << endl;
        return false;
    }
    if (GlobalArgs.minimumFocusStep < 0)
    {
        cerr << "Invalid minimum focus step argument." << endl;
        return false;
    }
    return true;
}

int main(int argc, char ** argv)
{
    if (!parseArguments(argc, argv))
    {
        showHelp(argv[0], false);
        return -1;
    }
    VideoCapture cap(GlobalArgs.deviceName);
    if (!cap.isOpened())
    {
        cout << "Cannot find device " << GlobalArgs.deviceName << endl;
        showHelp(argv[0], false);
        return -1;
    }

    VideoWriter videoWriter;
    Mat frame;
    FocusState state = createInitialState();
    bool focus = true;
    bool lastSucceeded = true;
    namedWindow(windowOriginal, 1);

    // Get settings:
    if (GlobalArgs.verbose)
    {
        if ((cap.get(CAP_PROP_GPHOTO2_WIDGET_ENUMERATE) == 0)
                || (cap.get(CAP_PROP_GPHOTO2_WIDGET_ENUMERATE) == -1))
        {
            // Some VideoCapture implementations can return -1, 0.
            cout << "This is not GPHOTO2 device." << endl;
            return -2;
        }
        cout << "List of camera settings: " << endl
                << (const char *) (intptr_t) cap.get(CAP_PROP_GPHOTO2_WIDGET_ENUMERATE)
                << endl;
        cap.set(CAP_PROP_GPHOTO2_COLLECT_MSGS, true);
    }

    cap.set(CAP_PROP_GPHOTO2_PREVIEW, true);
    cap.set(CAP_PROP_VIEWFINDER, true);
    cap >> frame; // To check PREVIEW output Size.
    if (!GlobalArgs.output.empty())
    {
        Size S = Size((int) cap.get(CAP_PROP_FRAME_WIDTH), (int) cap.get(CAP_PROP_FRAME_HEIGHT));
        int fourCC = CV_FOURCC('M', 'J', 'P', 'G');
        videoWriter.open(GlobalArgs.output, fourCC, GlobalArgs.fps, S, true);
        if (!videoWriter.isOpened())
        {
            cerr << "Cannot open output file " << GlobalArgs.output << endl;
            showHelp(argv[0], false);
            return -1;
        }
    }
    showHelp(argv[0], true); // welcome msg

    if (GlobalArgs.minimumFocusStep == 0)
    {
        state.minFocusStep = findMinFocusStep(cap, FOCUS_STEP / 16, -FOCUS_DIRECTION_INFTY);
    }
    else
    {
        state.minFocusStep = GlobalArgs.minimumFocusStep;
    }
    focusDriveEnd(cap, -FOCUS_DIRECTION_INFTY); // Start with closest

    char key = 0;
    while (key != 'q' && key != 27 /*ESC*/)
    {
        cap >> frame;
        if (frame.empty())
        {
            break;
        }
        if (!GlobalArgs.output.empty())
        {
            videoWriter << frame;
        }

        if (focus && !GlobalArgs.measure)
        {
            int stepToCorrect = correctFocus(lastSucceeded, state, rateFrame(frame));
            lastSucceeded = cap.set(CAP_PROP_ZOOM,
                    max(stepToCorrect, state.minFocusStep) * state.direction);
            if ((!lastSucceeded) || (stepToCorrect < state.minFocusStep))
            {
                if (--GlobalArgs.breakLimit <= 0)
                {
                    focus = false;
                    state.step = state.minFocusStep * 4;
                    cout << "In focus, you can press 'f' to improve with small step, "
                            "or 'r' to reset." << endl;
                }
            }
            else
            {
                GlobalArgs.breakLimit = DEFAULT_BREAK_LIMIT;
            }
        }
        else if (GlobalArgs.measure)
        {
            double rate = rateFrame(frame);
            if (!cap.set(CAP_PROP_ZOOM, state.minFocusStep))
            {
                if (--GlobalArgs.breakLimit <= 0)
                {
                    break;
                }
            }
            else
            {
                cout << rate << endl;
            }
        }

        if ((focus || GlobalArgs.measure) && GlobalArgs.verbose)
        {
            cout << "STATE\t" << state << endl;
            cout << "Output from camera: " << endl
                    << (const char *) (intptr_t) cap.get(CAP_PROP_GPHOTO2_FLUSH_MSGS) << endl;
        }

        imshow(windowOriginal, frame);
        switch (key = static_cast<char>(waitKey(30)))
        {
            case 'k': // focus out
                cap.set(CAP_PROP_ZOOM, 100);
                break;
            case 'j': // focus in
                cap.set(CAP_PROP_ZOOM, -100);
                break;
            case ',': // Drive to closest
                focusDriveEnd(cap, -FOCUS_DIRECTION_INFTY);
                break;
            case '.': // Drive to infinity
                focusDriveEnd(cap, FOCUS_DIRECTION_INFTY);
                break;
            case 'r': // reset focus state
                focus = true;
                state = createInitialState();
                break;
            case 'f': // focus switch on/off
                focus ^= true;
                break;
        }
    }

    if (GlobalArgs.verbose)
    {
        cout << "Captured " << (int) cap.get(CAP_PROP_FRAME_COUNT) << " frames"
                << endl << "in " << (int) (cap.get(CAP_PROP_POS_MSEC) / 1e2)
                << " seconds," << endl << "at avg speed "
                << (cap.get(CAP_PROP_FPS)) << " fps." << endl;
    }

    return 0;
}
