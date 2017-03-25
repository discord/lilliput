#include "opencv.hpp"
#include <stdbool.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>


opencv_Mat opencv_createMat(int width, int height, int type) {
    return new cv::Mat(width, height, type);
}

opencv_Mat opencv_createMatFromData(int width, int height, int type, void *data) {
    return new cv::Mat(width, height, type, data);
}

opencv_Mat opencv_imdecode(const opencv_Mat buf, int iscolor, opencv_Mat dst) {
    const cv::Mat *mat = static_cast<const cv::Mat *>(buf);
    cv::Mat *matDst = static_cast<cv::Mat *>(dst);
    return new cv::Mat(cv::imdecode(*mat, cv::IMREAD_COLOR, matDst));
}

bool opencv_imencode(const char *ext, const opencv_Mat image, void *dst, size_t dst_len, const int *params, size_t params_len, int *new_len) {
    std::vector<int> vParams;
    if (params) {
        vParams.assign(params, params+params_len);
    }
    uint8_t *dstBytes = (uint8_t *)dst;
    std::vector<uchar> buf;
    bool ret = cv::imencode(cv::String(ext), *static_cast<const cv::Mat *>(image), buf, vParams);
    std::copy(buf.begin(), buf.end(), dstBytes);
    *new_len = buf.size();
    return ret;
}

void opencv_resize(const opencv_Mat src, opencv_Mat dst, int width, int height, int interpolation) {
    cv::resize(*static_cast<const cv::Mat *>(src), *static_cast<cv::Mat *>(dst), cv::Size(width, height), 0, 0, interpolation);
}

opencv_Mat opencv_crop(opencv_Mat src, int x, int y, int width, int height) {
    cv::Mat *ret = new cv::Mat;
    *ret = (*static_cast<cv::Mat *>(src))(cv::Rect(x, y, width, height));
    return ret;
}
