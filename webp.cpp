#include "webp.hpp"
#include <opencv2/imgproc.hpp>
#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <stdbool.h>

struct webp_decoder_struct {
    WebPMux* mux;
    WebPMuxFrameInfo frame;
    WebPBitstreamFeatures features;
};

struct webp_encoder_struct {
    uint8_t* dst;
    size_t dst_len;
    const uint8_t* icc;
    size_t icc_len;
};

webp_decoder webp_decoder_create(const opencv_mat buf)
{
    auto cvMat = static_cast<const cv::Mat*>(buf);
    WebPData src = { cvMat->data, cvMat->total() };
    WebPMux* mux = WebPMuxCreate(&src, 0);

    if (!mux) {
        return nullptr;
    }

    WebPMuxFrameInfo frame;
    if (WebPMuxGetFrame(mux, 1, &frame) != WEBP_MUX_OK) {
        WebPMuxDelete(mux);
        return nullptr;
    }

    WebPBitstreamFeatures features;
    if (WebPGetFeatures(frame.bitstream.bytes, frame.bitstream.size, &features) != VP8_STATUS_OK) {
        WebPMuxDelete(mux);
        return nullptr;
    }

    if (features.has_animation) {
        WebPMuxDelete(mux);
        return nullptr;
    }

    webp_decoder d = new struct webp_decoder_struct();
    memset(d, 0, sizeof(struct webp_decoder_struct));
    d->mux = mux;
    d->frame = frame;
    d->features = features;
    return d;
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

int webp_decoder_get_num_frames(const webp_decoder d)
{
    // Retrieve the feature flags
    uint32_t flags;
    if (WebPMuxGetFeatures(d->mux, &flags) != WEBP_MUX_OK) {
        return 1;
    }

    // Check if the ANIMATION flag is set
    bool has_animation = (flags & ANIMATION_FLAG) != 0;
    return has_animation ? 2 : 1;
}

size_t webp_decoder_get_icc(const webp_decoder d, void* dst, size_t dst_len)
{
    WebPData icc = { nullptr, 0 };
    auto res = WebPMuxGetChunk(d->mux, "ICCP", &icc);
    if (icc.size > 0 && res == WEBP_MUX_OK) {
        if (icc.size <= dst_len) {
            memcpy(dst, icc.bytes, icc.size);
            return icc.size;
        }
    }
    return 0;
}

bool webp_decoder_decode(const webp_decoder d, opencv_mat mat)
{
    if (!d) {
        return false;
    }

    auto cvMat = static_cast<cv::Mat*>(mat);

    bool success = false;
    if (d->features.width > 0 && d->features.height > 0) {
        uint8_t *res;
        int row_size = cvMat->cols * cvMat->elemSize();
        if (d->features.has_alpha) {
            res = WebPDecodeBGRAInto(d->frame.bitstream.bytes, d->frame.bitstream.size,
                                     cvMat->data, cvMat->rows * cvMat->step, row_size);
        }
        else {
            res = WebPDecodeBGRInto(d->frame.bitstream.bytes, d->frame.bitstream.size,
                                    cvMat->data, cvMat->rows * cvMat->step, row_size);
        }

        success = res != nullptr;
    }

    return success;
}

void webp_decoder_release(webp_decoder d)
{
    WebPDataClear(&d->frame.bitstream);
    WebPMuxDelete(d->mux);
    delete d;
}

webp_encoder webp_encoder_create(void* buf, size_t buf_len, const void* icc, size_t icc_len)
{
    webp_encoder e = new struct webp_encoder_struct();
    memset(e, 0, sizeof(struct webp_encoder_struct));
    e->dst = (uint8_t*)(buf);
    e->dst_len = buf_len;
    if (icc_len) {
        e->icc = (const uint8_t*)(icc);
        e->icc_len = icc_len;
    }
    return e;
}

size_t webp_encoder_write(webp_encoder e, const opencv_mat src, const int* opt, size_t opt_len)
{
    if (!src) {
        // Input matrix pointer is null
        return 0;
    }

    auto mat = static_cast<const cv::Mat*>(src);
    if (!mat || mat->empty()) {
        // Invalid or empty OpenCV matrix
        return 0;
    }

    float quality = 100.0f;
    for (size_t i = 0; i + 1 < opt_len; i += 2) {
        if (opt[i] == CV_IMWRITE_WEBP_QUALITY) {
            quality = std::max(1.0f, (float)opt[i + 1]);
        }
    }

    if (mat->depth() != CV_8U) {
        // Image depth is not 8-bit unsigned
        return 0;
    }

    cv::Mat grayscaleConversionMat;
    if (mat->channels() == 1) {
        // for grayscale images, construct a temporary source
        cv::cvtColor(*mat, grayscaleConversionMat, CV_GRAY2BGR);
        if (grayscaleConversionMat.empty()) {
            // failed to convert grayscale image to BGR
            return 0;
        }
        mat = &grayscaleConversionMat;
    }

    if (mat->channels() != 3 && mat->channels() != 4) {
        // Image must have 3 or 4 channels
        return 0;
    }

    // webp will always allocate a region for the compressed image
    // we will have to copy from it, then deallocate this region
    size_t size = 0;
    uint8_t* out_picture = nullptr;

    if (quality > 100.0f) {
        if (mat->channels() == 3) {
            size = WebPEncodeLosslessBGR(mat->data, mat->cols, mat->rows, mat->step, &out_picture);
        } else if (mat->channels() == 4) {
            size = WebPEncodeLosslessBGRA(mat->data, mat->cols, mat->rows, mat->step, &out_picture);
        }
    }
    else {
        if (mat->channels() == 3) {
            size = WebPEncodeBGR(mat->data, mat->cols, mat->rows, mat->step, quality, &out_picture);
        } else if (mat->channels() == 4) {
            size = WebPEncodeBGRA(mat->data, mat->cols, mat->rows, mat->step, quality, &out_picture);
        }
    }

    if (size == 0) {
        // Failed to encode image
        return 0;
    }

    // Create a mux object and add the image to it
    // Then add the ICC profile if it exists
    WebPMux* mux = WebPMuxNew();
    WebPData picture = { out_picture, size };
    WebPMuxSetImage(mux, &picture, 0);

    if (e->icc) {
        WebPData icc_data = { e->icc, e->icc_len };
        WebPMuxSetChunk(mux, "ICCP", &icc_data, 0);
    }

    WebPData out_mux = { nullptr, 0 };
    WebPMuxAssemble(mux, &out_mux);

    WebPMuxDelete(mux);

    size_t copied = 0;
    if (out_mux.size) {
        if (out_mux.size < e->dst_len) {
            memcpy(e->dst, out_mux.bytes, out_mux.size);
            copied = out_mux.size;
        }
        WebPDataClear(&out_mux);
    }

    WebPFree(out_picture);
    return copied;
}

void webp_encoder_release(webp_encoder e)
{
    delete e;
}
