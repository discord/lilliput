#include "opencv.hpp"
#include <stdbool.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

opencv_mat opencv_mat_create(int width, int height, int type) {
    return new cv::Mat(height, width, type);
}

opencv_mat opencv_mat_create_from_data(int width, int height, int type, void *data, size_t data_len) {
    size_t total_size = width * height * CV_ELEM_SIZE(type);
    if (total_size > data_len) {
        return NULL;
    }
    return new cv::Mat(height, width, type, data);
}

void opencv_mat_release(opencv_mat mat) {
    auto m = static_cast<cv::Mat *>(mat);
    delete m;
}

vec vec_create() {
    return new std::vector<uchar>;
}

void vec_release(vec v) {
    delete static_cast<std::vector<uchar> *>(v);
}

size_t vec_copy(const vec v, void *buf, size_t buf_cap) {
    auto src = static_cast<std::vector<uchar> *>(v);
    if (src->size() > buf_cap) {
        return 0;
    }
    auto dst = static_cast<uint8_t *>(buf);
    std::copy(src->begin(), src->end(), dst);
    return src->end() - src->begin();
}

size_t vec_size(const vec v) {
    auto src = static_cast<std::vector<uchar> *>(v);
    return src->size();
}

void vec_clear(vec v) {
    auto src = static_cast<std::vector<uchar> *>(v);
    src->clear();
}

int opencv_type_depth(int type) {
    return CV_ELEM_SIZE1(type) * 8;
}

int opencv_type_channels(int type) {
    return CV_MAT_CN(type);
}

opencv_decoder opencv_decoder_create(const opencv_mat buf) {
    auto mat = static_cast<const cv::Mat *>(buf);
    auto d_ptr = cv::findDecoder(*mat);
    if (!d_ptr) {
        return NULL;
    }
    return new cv::Ptr<cv::ImageDecoder>(d_ptr);
}

const char *opencv_decoder_get_description(const opencv_decoder d) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return (*d_ptr)->getDescription().c_str();
}

void opencv_decoder_release(opencv_decoder d) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    delete d_ptr;
}

bool opencv_decoder_set_source(opencv_decoder d, const opencv_mat buf) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    // setSource() takes a const reference so we have to deref here
    return (*d_ptr)->setSource(*static_cast<const cv::Mat *>(buf));
}

bool opencv_decoder_read_header(opencv_decoder d) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return (*d_ptr)->readHeader();
}

int opencv_decoder_get_width(const opencv_decoder d) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return (*d_ptr)->width();
}

int opencv_decoder_get_height(const opencv_decoder d) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return (*d_ptr)->height();
}

int opencv_decoder_get_pixel_type(const opencv_decoder d) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return (*d_ptr)->type();
}

int opencv_decoder_get_orientation(const opencv_decoder d) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    return (*d_ptr)->orientation();
}

bool opencv_decoder_read_data(opencv_decoder d, opencv_mat dst) {
    auto d_ptr = static_cast<cv::Ptr<cv::ImageDecoder> *>(d);
    auto *mat = static_cast<cv::Mat *>(dst);
    return (*d_ptr)->readData(*mat);
}

opencv_encoder opencv_encoder_create(const char *ext) {
    auto enc = cv::findEncoder(cv::String(ext));
    if (!enc) {
        return NULL;
    }

    return new cv::Ptr<cv::ImageEncoder>(enc);
}

void opencv_encoder_release(opencv_encoder e) {
    auto e_ptr = static_cast<cv::Ptr<cv::ImageEncoder> *>(e);
    delete e_ptr;
}

bool opencv_encoder_set_destination(opencv_encoder e, vec dst) {
    auto e_ptr = static_cast<cv::Ptr<cv::ImageEncoder> *>(e);
    return (*e_ptr)->setDestination(*static_cast<std::vector<uchar> *>(dst));
}

bool opencv_encoder_write(opencv_encoder e, const opencv_mat src, const int *opt, size_t opt_len) {
    auto e_ptr = static_cast<cv::Ptr<cv::ImageEncoder> *>(e);
    auto mat = static_cast<const cv::Mat *>(src);
    std::vector<int> params;
    for (size_t i = 0; i < opt_len; i++) {
        params.push_back(opt[i]);
    }
    return (*e_ptr)->write(*mat, params);
};

void opencv_mat_resize(const opencv_mat src, opencv_mat dst, int width, int height, int interpolation) {
    cv::resize(*static_cast<const cv::Mat *>(src), *static_cast<cv::Mat *>(dst), cv::Size(width, height), 0, 0, interpolation);
}

opencv_mat opencv_mat_crop(const opencv_mat src, int x, int y, int width, int height) {
    auto ret = new cv::Mat;
    *ret = (*static_cast<const cv::Mat *>(src))(cv::Rect(x, y, width, height));
    return ret;
}

void opencv_mat_orientation_transform(CVImageOrientation orientation, opencv_mat mat) {
    auto cvMat = static_cast<cv::Mat *>(mat);
    cv::OrientationTransform(int(orientation), *cvMat);
}

int opencv_mat_get_width(const opencv_mat mat) {
    auto cvMat = static_cast<const cv::Mat *>(mat);
    return cvMat->cols;
}

int opencv_mat_get_height(const opencv_mat mat) {
    auto cvMat = static_cast<const cv::Mat *>(mat);
    return cvMat->rows;
}
