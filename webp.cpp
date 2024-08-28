#include "webp.hpp"
#include <opencv2/imgproc.hpp>
#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <stdbool.h>

struct webp_decoder_struct {
    WebPMux* mux;
    WebPBitstreamFeatures features;
    bool has_animation;
    uint32_t bgcolor;

    int current_frame_index;
    int prev_frame_delay_time;
    int prev_frame_x_offset;
    int prev_frame_y_offset;
    WebPMuxAnimDispose prev_frame_dispose;
    WebPMuxAnimBlend prev_frame_blend;
};

struct webp_encoder_struct {
    // input fields
    const uint8_t* icc;
    size_t icc_len;
    uint32_t bgcolor;

    // output fields
    WebPMux* mux;
    int frame_count;
    int first_frame_delay;
    int first_frame_blend;
    int first_frame_dispose;
    int first_frame_x_offset;
    int first_frame_y_offset;
    uint8_t* dst;
    size_t dst_len;
};

webp_decoder webp_decoder_create(const opencv_mat buf)
{

    auto cvMat = static_cast<const cv::Mat*>(buf);
    WebPData src = { cvMat->data, cvMat->total() };
    WebPMux* mux = WebPMuxCreate(&src, 0);

    if (!mux) {
        return nullptr;
    }

    // Get features at the container level
    uint32_t flags;
    if (WebPMuxGetFeatures(mux, &flags) != WEBP_MUX_OK) {
        WebPMuxDelete(mux);
        return nullptr;
    }

    bool has_animation = (flags & ANIMATION_FLAG) != 0;

    // Get the first frame to retrieve the image dimensions
    WebPMuxFrameInfo frame;
    if (WebPMuxGetFrame(mux, 1, &frame) != WEBP_MUX_OK) {
        WebPMuxDelete(mux);
        return nullptr;
    }

    WebPBitstreamFeatures features;
    if (WebPGetFeatures(frame.bitstream.bytes, frame.bitstream.size, &features) != VP8_STATUS_OK) {
        WebPDataClear(&frame.bitstream);
        WebPMuxDelete(mux);
        return nullptr;
    }
    WebPDataClear(&frame.bitstream);

    webp_decoder d = new struct webp_decoder_struct();
    memset(d, 0, sizeof(struct webp_decoder_struct));
    d->mux = mux;
    d->current_frame_index = 1;
    d->features = features;
    d->has_animation = has_animation;
    if (has_animation) {
        WebPMuxAnimParams anim_params;
        if (WebPMuxGetAnimationParams(mux, &anim_params) == WEBP_MUX_OK) {
            d->bgcolor = anim_params.bgcolor;
        } else {
            d->bgcolor = 0xFFFFFFFF; // White background
        }
    } else {
        d->bgcolor = 0xFFFFFFFF; // White background
    }

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
    return CV_8UC4;
}

int webp_decoder_get_prev_frame_delay(const webp_decoder d)
{
    return d->prev_frame_delay_time;
}

int webp_decoder_get_prev_frame_x_offset(const webp_decoder d)
{
    return d->prev_frame_x_offset;
}

int webp_decoder_get_prev_frame_y_offset(const webp_decoder d)
{
    return d->prev_frame_y_offset;
}

int webp_decoder_get_prev_frame_dispose(const webp_decoder d)
{
    return d->prev_frame_dispose;
}

int webp_decoder_get_prev_frame_blend(const webp_decoder d)
{
    return d->prev_frame_blend;
}

uint32_t webp_decoder_get_bg_color(const webp_decoder d)
{
    return d->bgcolor;
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

int webp_decoder_has_more_frames(webp_decoder d) {
    WebPMuxFrameInfo frame;
    WebPMuxError mux_error = WebPMuxGetFrame(d->mux, d->current_frame_index, &frame);
    if (mux_error != WEBP_MUX_OK) {
        return 0;
    }
    WebPDataClear(&frame.bitstream);
    return 1;
}

void webp_decoder_advance_frame(webp_decoder d) {
    d->current_frame_index++; // Advance to the next frame
}

bool webp_decoder_decode(const webp_decoder d, opencv_mat mat)
{
    if (!d) {
        return false;
    }

    WebPMuxFrameInfo frame;
    WebPMuxError mux_error = WebPMuxGetFrame(d->mux, d->current_frame_index, &frame);
    
    if (mux_error != WEBP_MUX_OK) {
        return false;
    }

    WebPBitstreamFeatures features;
    if (WebPGetFeatures(frame.bitstream.bytes, frame.bitstream.size, &features) != VP8_STATUS_OK) {
        WebPDataClear(&frame.bitstream);
        return false;
    }

    // Set the cv::Mat dimensions to the frame's width and height
    auto cvMat = static_cast<cv::Mat*>(mat);
    cvMat->create(features.height, features.width, features.has_alpha ? CV_8UC4 : CV_8UC3);

    // Recalculate row size based on the new dimensions
    int row_size = cvMat->cols * cvMat->elemSize();
    uint8_t* res = nullptr;

    // Store frame properties for future use
    d->prev_frame_delay_time = frame.duration;
    d->prev_frame_x_offset = frame.x_offset;
    d->prev_frame_y_offset = frame.y_offset;
    d->prev_frame_dispose = frame.dispose_method;
    d->prev_frame_blend = frame.blend_method;

    if (features.has_alpha) {
        res = WebPDecodeBGRAInto(frame.bitstream.bytes, frame.bitstream.size,
                                 cvMat->data, cvMat->rows * cvMat->step, row_size);
    } else {
        res = WebPDecodeBGRInto(frame.bitstream.bytes, frame.bitstream.size,
                                cvMat->data, cvMat->rows * cvMat->step, row_size);
    }

    WebPDataClear(&frame.bitstream);
    return res;
}

void webp_decoder_release(webp_decoder d)
{
    WebPMuxDelete(d->mux);
    delete d;
}

webp_encoder webp_encoder_create(void* buf, size_t buf_len, const void* icc, size_t icc_len, uint32_t bgcolor)
{
    webp_encoder e = new struct webp_encoder_struct();
    memset(e, 0, sizeof(struct webp_encoder_struct));
    e->dst = (uint8_t*)(buf);
    e->dst_len = buf_len;
    e->mux = WebPMuxNew();
    e->frame_count = 1;
    e->first_frame_delay = 0;
    e->bgcolor = bgcolor;
    if (icc_len) {
        e->icc = (const uint8_t*)(icc);
        e->icc_len = icc_len;
    }
    return e;
}

size_t webp_encoder_write(webp_encoder e, const opencv_mat src, const int* opt, size_t opt_len, int delay, int blend, int dispose, int x_offset, int y_offset) {
    // if the source is null, finalize the animation/image and return the size of the output buffer
    if (!src) {
        if (e->frame_count == 1) {
            // No frames were added
            WebPMuxDelete(e->mux);
            e->mux = nullptr;
            return 0;
        }

        // Finalize the animation/image and return the size of the output buffer
        WebPData out_mux = { nullptr, 0 };
        WebPMuxError mux_error = WebPMuxAssemble(e->mux, &out_mux);

        if (mux_error != WEBP_MUX_OK) {
            return 0;
        }

        if (out_mux.size == 0) {
            return 0;
        }

        size_t copied = 0;
        if (out_mux.size < e->dst_len) {
            memcpy(e->dst, out_mux.bytes, out_mux.size);
            copied = out_mux.size;
        }

        WebPDataClear(&out_mux);
        WebPMuxDelete(e->mux);
        e->mux = nullptr; // Ensure the mux is no longer used

        return copied;
    }

    // Encode the source image
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
        } else {
            size = WebPEncodeLosslessBGRA(mat->data, mat->cols, mat->rows, mat->step, &out_picture);
        }
    } else {
        if (mat->channels() == 3) {
            size = WebPEncodeBGR(mat->data, mat->cols, mat->rows, mat->step, quality, &out_picture);
        } else {
            size = WebPEncodeBGRA(mat->data, mat->cols, mat->rows, mat->step, quality, &out_picture);
        }
    }

    if (size == 0) {
        // Failed to encode image
        return 0;
    }

    WebPData picture = { out_picture, size };
    if (e->frame_count == 1) {
        // First frame handling
        e->first_frame_delay = delay;
        e->first_frame_blend = blend;
        e->first_frame_dispose = dispose;
        e->first_frame_x_offset = x_offset;
        e->first_frame_y_offset = y_offset;

        // Add ICC profile to the mux object
        if (e->icc) {
            WebPData icc_data = { e->icc, e->icc_len };
            WebPMuxError mux_error = WebPMuxSetChunk(e->mux, "ICCP", &icc_data, 0);
        }

        WebPMuxError mux_error = WebPMuxSetImage(e->mux, &picture, 1);
        if (mux_error != WEBP_MUX_OK) {
            WebPFree(out_picture);
            return 0;
        }
    } else {
        if (e->frame_count == 2) {
            // A second frame is provided: we need to recreate the mux for animation

            // Store the first frame
            WebPMuxFrameInfo first_frame;
            memset(&first_frame, 0, sizeof(WebPMuxFrameInfo));
            WebPMuxGetFrame(e->mux, 1, &first_frame);  // Get the first frame as it was set initially

            // Delete the old single-image mux and create a new one for animation
            WebPMuxDelete(e->mux);
            e->mux = WebPMuxNew();

            // Set the ICC profile if it exists
            if (e->icc && e->icc_len > 0) {
                WebPData icc_data = { e->icc, e->icc_len };
                WebPMuxError mux_error = WebPMuxSetChunk(e->mux, "ICCP", &icc_data, 1);
                WebPDataClear(&icc_data);
                if (mux_error != WEBP_MUX_OK) {
                    WebPFree(out_picture);
                    return 0;
                }
            }

            // Set animation parameters
            WebPMuxAnimParams anim_params;
            anim_params.loop_count = 0;  // Infinite loop
            anim_params.bgcolor = e->bgcolor;
            // anim_params.bgcolor = 0xFF000000;  // White background
            // TODO - add support for bgcolor by reading the ANIM chunk from the first frame

            WebPMuxError mux_error = WebPMuxSetAnimationParams(e->mux, &anim_params);
            if (mux_error != WEBP_MUX_OK) {
                WebPFree(out_picture);
                return 0;
            }

            // Convert the first frame into an ANMF chunk and add it to the new mux
            first_frame.id = WEBP_CHUNK_ANMF;
            first_frame.duration = e->first_frame_delay;
            first_frame.x_offset = e->first_frame_x_offset;
            first_frame.y_offset = e->first_frame_y_offset;
            first_frame.dispose_method = (WebPMuxAnimDispose)e->first_frame_dispose;
            first_frame.blend_method = (WebPMuxAnimBlend)e->first_frame_blend;
            mux_error = WebPMuxPushFrame(e->mux, &first_frame, 1);
            if (mux_error != WEBP_MUX_OK) {
                WebPFree(out_picture);
                return 0;
            }
            WebPDataClear(&first_frame.bitstream);
        }

        // Add the current frame as an ANMF chunk
        WebPMuxFrameInfo frame;
        memset(&frame, 0, sizeof(WebPMuxFrameInfo));
        frame.bitstream = picture;
        frame.id = WEBP_CHUNK_ANMF;
        frame.x_offset = x_offset;
        frame.y_offset = y_offset;
        frame.duration = delay;
        frame.dispose_method = (WebPMuxAnimDispose)dispose;
        frame.blend_method = (WebPMuxAnimBlend)blend;

        // Add the frame to the mux object
        WebPMuxError mux_error = WebPMuxPushFrame(e->mux, &frame, 1);
        if (mux_error != WEBP_MUX_OK) {
            WebPFree(out_picture);
            return 0;
        }
    }

    e->frame_count++;

    WebPFree(out_picture);
    return size;
}

void webp_encoder_release(webp_encoder e)
{
    WebPMuxDelete(e->mux);
    delete e;
}

size_t webp_encoder_flush(webp_encoder e)
{
    return webp_encoder_write(e, nullptr, nullptr, 0, 0, 0, 0, 0, 0);
}