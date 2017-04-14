#include <iostream>

#include "opencv2/opencv_modules.hpp"

#if defined(HAVE_OPENCV_CUDACODEC)

#include <string>
#include <vector>
#include <algorithm>
#include <numeric>

#include <opencv2/core.hpp>
#include <opencv2/core/opengl.hpp>
#include <opencv2/cudacodec.hpp>
#include <opencv2/highgui.hpp>

int main(int argc, const char* argv[])
{
    if (argc != 2)
        return -1;

    const std::string fname(argv[1]);

    cv::namedWindow("CPU", cv::WINDOW_NORMAL);
    cv::namedWindow("GPU", cv::WINDOW_OPENGL);
    cv::cuda::setGlDevice();

    cv::Mat frame;
    cv::VideoCapture reader(fname);

    cv::cuda::GpuMat d_frame;
    cv::Ptr<cv::cudacodec::VideoReader> d_reader = cv::cudacodec::createVideoReader(fname);

    cv::TickMeter tm;
    std::vector<double> cpu_times;
    std::vector<double> gpu_times;

    for (;;)
    {
        tm.reset(); tm.start();
        if (!reader.read(frame))
            break;
        tm.stop();
        cpu_times.push_back(tm.getTimeMilli());

        tm.reset(); tm.start();
        if (!d_reader->nextFrame(d_frame))
            break;
        tm.stop();
        gpu_times.push_back(tm.getTimeMilli());

        cv::imshow("CPU", frame);
        cv::imshow("GPU", d_frame);

        if (cv::waitKey(3) > 0)
            break;
    }

    if (!cpu_times.empty() && !gpu_times.empty())
    {
        std::cout << std::endl << "Results:" << std::endl;

        std::sort(cpu_times.begin(), cpu_times.end());
        std::sort(gpu_times.begin(), gpu_times.end());

        double cpu_avg = std::accumulate(cpu_times.begin(), cpu_times.end(), 0.0) / cpu_times.size();
        double gpu_avg = std::accumulate(gpu_times.begin(), gpu_times.end(), 0.0) / gpu_times.size();

        std::cout << "CPU : Avg : " << cpu_avg << " ms FPS : " << 1000.0 / cpu_avg << std::endl;
        std::cout << "GPU : Avg : " << gpu_avg << " ms FPS : " << 1000.0 / gpu_avg << std::endl;
    }

    return 0;
}

#else

int main()
{
    std::cout << "OpenCV was built without CUDA Video decoding support\n" << std::endl;
    return 0;
}

#endif
