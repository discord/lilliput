#include "webp.hpp"
#include <opencv2/imgproc.hpp>
#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <stdbool.h>

struct webp_decoder_struct {
    WebPMux* mux;
    int total_frame_count;
    uint32_t bgcolor;
    uint32_t loop_count;
    bool has_alpha;
    bool has_animation;
    int width;
    int height;

    int current_frame_index;
    int prev_frame_delay_time;
    int prev_frame_x_offset;
    int prev_frame_y_offset;
    WebPMuxAnimDispose prev_frame_dispose;
    WebPMuxAnimBlend prev_frame_blend;
    uint8_t* decode_buffer;
    size_t decode_buffer_size;
    int total_duration;
};

struct webp_encoder_struct {
    // input fields
    const uint8_t* icc;
    size_t icc_len;
    uint32_t bgcolor;
    uint32_t loop_count;

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

/**
 * Creates a WebP decoder from the given OpenCV matrix.
 * @param buf The input OpenCV matrix containing the WebP image data.
 * @return A pointer to the created webp_decoder_struct, or nullptr if creation failed.
 */
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

    webp_decoder d = new webp_decoder_struct();
    memset(d, 0, sizeof(webp_decoder_struct));
    d->mux = mux;
    d->current_frame_index = 1;
    d->has_alpha = (flags & ALPHA_FLAG);

    // Get the canvas size
    if (WebPMuxGetCanvasSize(mux, &d->width, &d->height) != WEBP_MUX_OK) {
        WebPMuxDelete(mux);
        return nullptr;
    }

    // Calculate total frame count and duration
    d->total_frame_count = 0;
    d->total_duration = 0;
    do {
        d->total_frame_count++;
        d->total_duration += frame.duration;
        WebPDataClear(&frame.bitstream);
    } while (WebPMuxGetFrame(mux, d->total_frame_count + 1, &frame) == WEBP_MUX_OK);

    // Get animation parameters
    d->bgcolor = 0xFFFFFFFF; // Default to white background
    if (flags & ANIMATION_FLAG) {
        WebPMuxAnimParams anim_params;
        if (WebPMuxGetAnimationParams(mux, &anim_params) == WEBP_MUX_OK) {
            d->bgcolor = anim_params.bgcolor;
            d->loop_count = anim_params.loop_count;
        }
        d->has_animation = true;
    } else {
        // For static images, ensure duration is 0
        d->total_duration = 0;
    }

    // Pre-allocate decode buffer
    d->decode_buffer_size = d->width * d->height * 4; // 4 channels for RGBA
    d->decode_buffer = new uint8_t[d->decode_buffer_size];

    return d;
}

/**
 * Gets the width of the WebP image.
 * @param d The webp_decoder_struct pointer.
 * @return The width of the WebP image.
 */
int webp_decoder_get_width(const webp_decoder d)
{
    return d->width;
}

/**
 * Gets the height of the WebP image.
 * @param d The webp_decoder_struct pointer.
 * @return The height of the WebP image.
 */
int webp_decoder_get_height(const webp_decoder d)
{
    return d->height;
}

/**
 * Gets the pixel type of the WebP image.
 * @param d The webp_decoder_struct pointer.
 * @return The pixel type of the WebP image.
 */
int webp_decoder_get_pixel_type(const webp_decoder d)
{
    return d->has_alpha ? CV_8UC4 : CV_8UC3;
}

/**
 * Gets the delay time of the previous frame.
 * @param d The webp_decoder_struct pointer.
 * @return The delay time of the previous frame.
 */
int webp_decoder_get_prev_frame_delay(const webp_decoder d)
{
    return d->prev_frame_delay_time;
}

/**
 * Gets the x-offset of the previous frame.
 * @param d The webp_decoder_struct pointer.
 * @return The x-offset of the previous frame.
 */
int webp_decoder_get_prev_frame_x_offset(const webp_decoder d)
{
    return d->prev_frame_x_offset;
}

/**
 * Gets the y-offset of the previous frame.
 * @param d The webp_decoder_struct pointer.
 * @return The y-offset of the previous frame.
 */
int webp_decoder_get_prev_frame_y_offset(const webp_decoder d)
{
    return d->prev_frame_y_offset;
}

/**
 * Gets the dispose method of the previous frame.
 * @param d The webp_decoder_struct pointer.
 * @return The dispose method of the previous frame.
 */
int webp_decoder_get_prev_frame_dispose(const webp_decoder d)
{
    return d->prev_frame_dispose;
}

/**
 * Gets the blend method of the previous frame.
 * @param d The webp_decoder_struct pointer.
 * @return The blend method of the previous frame.
 */
int webp_decoder_get_prev_frame_blend(const webp_decoder d)
{
    return d->prev_frame_blend;
}

/**
 * Gets the background color of the WebP image.
 * @param d The webp_decoder_struct pointer.
 * @return The background color of the WebP image.
 */
uint32_t webp_decoder_get_bg_color(const webp_decoder d)
{
    return d->bgcolor;
}

/**
 * Gets the loop count of the WebP image.
 * @param d The webp_decoder_struct pointer.
 * @return The loop count of the WebP image.
 */
uint32_t webp_decoder_get_loop_count(const webp_decoder d)
{
    return d->loop_count;
}

/**
 * Gets the total number of frames in the WebP image.
 * @param d The webp_decoder_struct pointer.
 * @return The total number of frames in the WebP image.
 */
int webp_decoder_get_num_frames(const webp_decoder d)
{
    return d ? d->total_frame_count : 0;
}

/**
 * Gets the total duration of the WebP animation in milliseconds.
 * @param d The webp_decoder_struct pointer.
 * @return The total duration in milliseconds, 0 for static images.
 */
int webp_decoder_get_total_duration(const webp_decoder d)
{
    return d ? d->total_duration : 0;
}

/**
 * Gets the ICC profile data from the WebP image.
 * @param d The webp_decoder_struct pointer.
 * @param dst The destination buffer to store the ICC profile data.
 * @param dst_len The size of the destination buffer.
 * @return The size of the ICC profile data copied to the destination buffer.
 */
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

/**
 * Checks if there are more frames to decode in the WebP image.
 * @param d The webp_decoder_struct pointer.
 * @return True if there are more frames to decode, false otherwise.
 */
int webp_decoder_has_more_frames(webp_decoder d)
{
     return d->current_frame_index < d->total_frame_count;
}

/**
 * Advances to the next frame in the WebP image.
 * @param d The webp_decoder_struct pointer.
 */
void webp_decoder_advance_frame(webp_decoder d)
{
    d->current_frame_index++;
}

/**
 * Decodes the current frame of the WebP image and stores the decoded image in the provided OpenCV matrix.
 * @param d The webp_decoder_struct pointer.
 * @param mat The OpenCV matrix to store the decoded image.
 * @return True if the frame was successfully decoded, false otherwise.
 */
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
    cvMat->create(features.height, features.width, webp_decoder_get_pixel_type(d));

    // Recalculate row size based on the new dimensions
    int row_size = cvMat->cols * cvMat->elemSize();

    // Store frame properties for future use
    d->prev_frame_delay_time = frame.duration;
    d->prev_frame_x_offset = frame.x_offset;
    d->prev_frame_y_offset = frame.y_offset;
    d->prev_frame_dispose = frame.dispose_method;
    d->prev_frame_blend = frame.blend_method;

    // Decode the frame
    uint8_t* res = nullptr;
    switch (webp_decoder_get_pixel_type(d)) {
        case CV_8UC4:
            res = WebPDecodeBGRAInto(frame.bitstream.bytes, frame.bitstream.size,
                                     d->decode_buffer, d->decode_buffer_size, row_size);
            break;
        case CV_8UC3:
            res = WebPDecodeBGRInto(frame.bitstream.bytes, frame.bitstream.size,
                                d->decode_buffer, d->decode_buffer_size, row_size);
            break;
        default:
            return false;
    }

    if (res) {
        memcpy(cvMat->data, d->decode_buffer, cvMat->total() * cvMat->elemSize());
    }

    WebPDataClear(&frame.bitstream);
    return res != nullptr;
}

/**
 * Releases the resources allocated for the webp_decoder_struct.
 * @param d The webp_decoder_struct pointer.
 */
void webp_decoder_release(webp_decoder d)
{
    if (d) {
        if (d->mux) WebPMuxDelete(d->mux);
        delete[] d->decode_buffer;
        delete d;
    }
}

/**
 * Creates a WebP encoder with the given parameters.
 * @param buf The output buffer to store the encoded WebP data.
 * @param buf_len The size of the output buffer.
 * @param icc The ICC profile data.
 * @param icc_len The size of the ICC profile data.
 * @param bgcolor The background color for the WebP image.
 * @return A pointer to the created webp_encoder_struct, or nullptr if creation failed.
 */
webp_encoder webp_encoder_create(void* buf, size_t buf_len, const void* icc, size_t icc_len, uint32_t bgcolor, int loop_count)
{
    webp_encoder e = new struct webp_encoder_struct();
    memset(e, 0, sizeof(struct webp_encoder_struct));
    e->dst = (uint8_t*)(buf);
    e->dst_len = buf_len;
    e->mux = WebPMuxNew();
    e->frame_count = 1;
    e->first_frame_delay = 0;
    e->bgcolor = bgcolor;
    e->loop_count = loop_count;
    if (icc_len) {
        e->icc = (const uint8_t*)(icc);
        e->icc_len = icc_len;
    }
    return e;
}

/**
 * Encodes the given OpenCV matrix as a WebP image and writes the encoded data to the output buffer.
 * @param e The webp_encoder_struct pointer.
 * @param src The OpenCV matrix containing the image to encode.
 * @param opt The encoding options.
 * @param opt_len The number of encoding options.
 * @param delay The delay time for the current frame.
 * @param blend The blend method for the current frame.
 * @param dispose The dispose method for the current frame.
 * @param x_offset The x-offset for the current frame.
 * @param y_offset The y-offset for the current frame.
 * @return The size of the encoded WebP data, or 0 if encoding failed.
 */
size_t webp_encoder_write(webp_encoder e, const opencv_mat src, const int* opt, size_t opt_len, int delay, int blend, int dispose, int x_offset, int y_offset)
{
    if (!e || !e->mux) {
        return 0;
    }

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
            if (out_picture) {
                WebPFree(out_picture);
            }
            return 0;
        }
    } else {
        if (e->frame_count == 2) {
            // A second frame is provided: we need to recreate the mux for animation

            // Store the first frame
            WebPMuxFrameInfo first_frame;
            memset(&first_frame, 0, sizeof(WebPMuxFrameInfo));
            WebPMuxError get_frame_error = WebPMuxGetFrame(e->mux, 1, &first_frame);
            if (get_frame_error != WEBP_MUX_OK) {
                if (out_picture) {
                    WebPFree(out_picture);
                }
                return 0;
            }

            // Delete the old single-image mux and create a new one for animation
            WebPMuxDelete(e->mux);
            e->mux = WebPMuxNew();

            // Set the ICC profile if it exists
            if (e->icc && e->icc_len > 0) {
                WebPData icc_data = { e->icc, e->icc_len };
                WebPMuxError mux_error = WebPMuxSetChunk(e->mux, "ICCP", &icc_data, 1);
                if (mux_error != WEBP_MUX_OK) {
                    if (out_picture) {
                        WebPFree(out_picture);
                    }
                    return 0;
                }
            }

            // Set animation parameters
            WebPMuxAnimParams anim_params;
            anim_params.loop_count = e->loop_count;
            anim_params.bgcolor = e->bgcolor;

            WebPMuxError anim_params_error = WebPMuxSetAnimationParams(e->mux, &anim_params);
            if (anim_params_error != WEBP_MUX_OK) {
                if (out_picture) {
                    WebPFree(out_picture);
                }
                return 0;
            }

            // Convert the first frame into an ANMF chunk and add it to the new mux
            first_frame.id = WEBP_CHUNK_ANMF;
            first_frame.duration = e->first_frame_delay;
            first_frame.x_offset = e->first_frame_x_offset;
            first_frame.y_offset = e->first_frame_y_offset;
            first_frame.dispose_method = (WebPMuxAnimDispose)e->first_frame_dispose;
            first_frame.blend_method = (WebPMuxAnimBlend)e->first_frame_blend;
            WebPMuxError push_frame_error = WebPMuxPushFrame(e->mux, &first_frame, 1);
            if (push_frame_error != WEBP_MUX_OK) {
                if (out_picture) {
                    WebPFree(out_picture);
                }
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
        WebPMuxError push_frame_error = WebPMuxPushFrame(e->mux, &frame, 1);
        if (push_frame_error != WEBP_MUX_OK) {
            if (out_picture) {
                WebPFree(out_picture);
            }
            return 0;
        }
    }

    e->frame_count++;

    if (out_picture) {
        WebPFree(out_picture);
    }
    return size;
}

/**
 * Releases the resources allocated for the webp_encoder_struct.
 * @param e The webp_encoder_struct pointer.
 */
void webp_encoder_release(webp_encoder e)
{
    if (e) {
        if (e->mux) {
            WebPMuxDelete(e->mux);
        }
        delete e;
    }
}

/**
 * Flushes the remaining data in the webp_encoder_struct and finalizes the WebP image.
 * @param e The webp_encoder_struct pointer.
 * @return The size of the encoded WebP data, or 0 if encoding failed.
 */
size_t webp_encoder_flush(webp_encoder e)
{
    return webp_encoder_write(e, nullptr, nullptr, 0, 0, 0, 0, 0, 0);
}