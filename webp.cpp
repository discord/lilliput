#include "webp.hpp"
#include <opencv2/imgproc.hpp>
#include <webp/decode.h>
#include <webp/encode.h>
#include <stdbool.h>

struct webp_decoder_struct {
    const cv::Mat* mat;
    WebPBitstreamFeatures features;
};

struct webp_encoder_struct {
    uint8_t* dst;
    size_t dst_len;
};

webp_decoder webp_decoder_create(const opencv_mat buf)
{
    webp_decoder d = new struct webp_decoder_struct();
    memset(d, 0, sizeof(struct webp_decoder_struct));
    d->mat = static_cast<const cv::Mat*>(buf);
    return d;
}

bool webp_decoder_read_header(webp_decoder d)
{
    return WebPGetFeatures(d->mat->data, d->mat->total(), &d->features) == VP8_STATUS_OK;
}

int webp_decoder_get_width(const webp_decoder d)
{
    return d->features.width;
}

int webp_decoder_get_height(const webp_decoder d)
{
    return d->features.height;
}

int webp_decoder_get_pixel_type(const webp_decoder d)
{
    return d->features.has_alpha ? CV_8UC4 : CV_8UC3;
}

bool webp_decoder_decode(const webp_decoder d, opencv_mat mat)
{
    if (!d) {
        return false;
    }

    auto cvMat = static_cast<cv::Mat*>(mat);

    if (d->features.width > 0 && d->features.height > 0) {
        uint8_t *res;
        int row_size = cvMat->cols * cvMat->elemSize();
        if (d->features.has_alpha) {
            res = WebPDecodeBGRAInto(d->mat->data, d->mat->total(),
                                     cvMat->data, cvMat->rows * cvMat->step, row_size);
        }
        else {
            res = WebPDecodeBGRInto(d->mat->data, d->mat->total(),
                                     cvMat->data, cvMat->rows * cvMat->step, row_size);
        }

        return res != nullptr;
    }

    return false;
}

void webp_decoder_release(webp_decoder d)
{
    delete d;
}

webp_encoder webp_encoder_create(void* buf, size_t buf_len)
{
    webp_encoder e = new struct webp_encoder_struct();
    memset(e, 0, sizeof(struct webp_encoder_struct));
    e->dst = (uint8_t*)(buf);
    e->dst_len = buf_len;
    return e;
}

size_t webp_encoder_write(webp_encoder e, const opencv_mat src, const int* opt, size_t opt_len)
{
    auto mat = static_cast<const cv::Mat*>(src);

    float quality = 100.0f;
    for (size_t i = 0; i + 1 < opt_len; i += 2) {
        if (opt[i] == CV_IMWRITE_WEBP_QUALITY) {
            quality = std::max(1.0f, (float)opt[i + 1]);
        }
    }

    if (mat->depth() != CV_8U) {
        return false;
    }

    cv::Mat temp;
    if (mat->channels() == 1) {
        // for grayscale images, construct a temporary source
        cv::cvtColor(*mat, temp, CV_GRAY2BGR);
        mat = &temp;
    }

    if (mat->channels() != 3 && mat->channels() != 4) {
        return false;
    }

    // webp will always allocate a region for the compressed image
    // we will have to copy from it, then deallocate this region
    size_t size = 0;
    uint8_t* out = nullptr;

    if (quality > 100.0f) {
        if (mat->channels() == 3) {
            size = WebPEncodeLosslessBGR(mat->data, mat->cols, mat->rows, mat->step, &out);
        } else if (mat->channels() == 4) {
            size = WebPEncodeLosslessBGRA(mat->data, mat->cols, mat->rows, mat->step, &out);
        }
    }
    else {
        if (mat->channels() == 3) {
            size = WebPEncodeBGR(mat->data, mat->cols, mat->rows, mat->step, quality, &out);
        } else if (mat->channels() == 4) {
            size = WebPEncodeBGRA(mat->data, mat->cols, mat->rows, mat->step, quality, &out);
        }
    }

    if (size > 0) {
        size_t copied = 0;
        if (size < e->dst_len) {
            memcpy(e->dst, out, size);
            copied = size;
        }

        WebPFree(out);
        return copied;
    }

    return 0;
}

void webp_encoder_release(webp_encoder e)
{
    delete e;
}