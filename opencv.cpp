#include "opencv.hpp"
#include <stdbool.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

opencv_Mat opencv_createMat(int width, int height, int type) {
    return new cv::Mat(height, width, type);
}

opencv_Mat opencv_createMatFromData(int width, int height, int type, void *data, size_t data_len) {
    size_t total_size = width * height * CV_ELEM_SIZE(type);
    if (total_size > data_len) {
        return NULL;
    }
    return new cv::Mat(height, width, type, data);
}

void opencv_mat_release(opencv_Mat mat) {
    cv::Mat *m = static_cast<cv::Mat *>(mat);
    delete m;
}

vec vec_create() {
    return new std::vector<uchar>;
}

void vec_destroy(vec v) {
    delete static_cast<std::vector<uchar> *>(v);
}

size_t vec_copy(const vec v, void *buf, size_t buf_len) {
    std::vector<uchar> src = *static_cast<std::vector<uchar> *>(v);
    if (src.size() > buf_len) {
        return 0;
    }
    uint8_t *dst = static_cast<uint8_t *>(buf);
    std::copy(src.begin(), src.end(), dst);
    return src.end() - src.begin();
}

size_t vec_size(const vec v) {
    std::vector<uchar> src = *static_cast<std::vector<uchar> *>(v);
    return src.size();
}

int opencv_type_depth(int type) {
    return CV_ELEM_SIZE1(type) * 8;
}

int opencv_type_channels(int type) {
    return CV_MAT_CN(type);
}

opencv_Decoder opencv_createDecoder(const opencv_Mat buf) {
    const cv::Mat *mat = static_cast<const cv::Mat *>(buf);
    cv::Ptr<cv::ImageDecoder> d_ptr = cv::findDecoder(*mat);
    if (!d_ptr) {
        return NULL;
    }
    return new cv::Ptr<cv::ImageDecoder>(d_ptr);
}

const char *opencv_decoder_get_description(const opencv_Decoder d) {
    cv::Ptr<cv::ImageDecoder> d_ptr = *static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return d_ptr->getDescription().c_str();
}

void opencv_decoder_release(opencv_Decoder d) {
    cv::Ptr<cv::ImageDecoder>* d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    delete d_ptr;
}

bool opencv_decoder_set_source(opencv_Decoder d, const opencv_Mat buf) {
    cv::Ptr<cv::ImageDecoder> d_ptr = *static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return d_ptr->setSource(*static_cast<const cv::Mat *>(buf));
}

bool opencv_decoder_read_header(opencv_Decoder d) {
    cv::Ptr<cv::ImageDecoder> d_ptr = *static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return d_ptr->readHeader();
}

int opencv_decoder_get_width(const opencv_Decoder d) {
    cv::Ptr<cv::ImageDecoder> d_ptr = *static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return d_ptr->width();
}

int opencv_decoder_get_height(const opencv_Decoder d) {
    cv::Ptr<cv::ImageDecoder> d_ptr = *static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return d_ptr->height();
}

int opencv_decoder_get_pixel_type(const opencv_Decoder d) {
    cv::Ptr<cv::ImageDecoder> d_ptr = *static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return d_ptr->type();
}

int opencv_decoder_get_orientation(const opencv_Decoder d) {
    cv::Ptr<cv::ImageDecoder> d_ptr = *static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return d_ptr->orientation();
}

bool opencv_decoder_read_data(opencv_Decoder d, opencv_Mat dst) {
    cv::Ptr<cv::ImageDecoder> d_ptr = *static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    cv::Mat *mat = static_cast<cv::Mat *>(dst);
    return d_ptr->readData(*mat);
}

opencv_Encoder opencv_createEncoder(const char *ext) {
    cv::Ptr<cv::ImageEncoder> enc = cv::findEncoder(cv::String(ext));
    if (!enc) {
        return NULL;
    }

    return new cv::Ptr<cv::ImageEncoder>(enc);
}

void opencv_encoder_release(opencv_Encoder e) {
    cv::Ptr<cv::ImageEncoder>* e_ptr = static_cast<cv::Ptr<cv::ImageEncoder> *>(e);
    delete e_ptr;
}

bool opencv_encoder_set_destination(opencv_Encoder e, vec dst) {
    cv::Ptr<cv::ImageEncoder> e_ptr = *static_cast<cv::Ptr<cv::ImageEncoder> *>(e);
    return e_ptr->setDestination(*static_cast<std::vector<uchar> *>(dst));
}

bool opencv_encoder_write(opencv_Encoder e, const opencv_Mat src, const int *opt, size_t opt_len) {
    cv::Ptr<cv::ImageEncoder> e_ptr = *static_cast<cv::Ptr<cv::ImageEncoder> *>(e);
    const cv::Mat *mat = static_cast<const cv::Mat *>(src);
    std::vector<int> params;
    for (size_t i = 0; i < opt_len; i++) {
        params.push_back(opt[i]);
    }
    return e_ptr->write(*mat, params);
};

bool opencv_imencode(const char *ext, const opencv_Mat image, void *dst, size_t dst_len, const int32_t *params, size_t params_len, int *new_len) {
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

opencv_Mat opencv_crop(const opencv_Mat src, int x, int y, int width, int height) {
    cv::Mat *ret = new cv::Mat;
    *ret = (*static_cast<const cv::Mat *>(src))(cv::Rect(x, y, width, height));
    return ret;
}

void opencv_mat_orientation_transform(CVImageOrientation orientation, opencv_Mat mat) {
    cv::Mat *cvMat = static_cast<cv::Mat *>(mat);
    cv::OrientationTransform(cv::ImageOrientation(orientation), *cvMat);
}

int opencv_mat_get_width(const opencv_Mat mat) {
    const cv::Mat *cvMat = static_cast<const cv::Mat *>(mat);
    return cvMat->cols;
}

int opencv_mat_get_height(const opencv_Mat mat) {
    const cv::Mat *cvMat = static_cast<const cv::Mat *>(mat);
    return cvMat->rows;
}
