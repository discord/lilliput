#include "avif.hpp"
#include <opencv2/imgproc.hpp>
#include <avif/avif.h>
#include <cstring>

#define DEFAULT_BACKGROUND_COLOR 0xFFFFFFFF

struct avif_decoder_struct {
    avifDecoder* decoder;
    avifRGBImage rgb;
    const uint8_t* buffer;
    size_t buffer_size;
    int frame_count;
    int current_frame;
    bool has_alpha;
    uint32_t bgcolor;
    int timescale;
    int total_duration;
};

struct avif_encoder_struct {
    avifEncoder* encoder;
    uint8_t* dst;
    size_t dst_len;
    const uint8_t* icc;
    size_t icc_len;
    int frame_count;
    bool has_alpha;
};

//----------------------
// Decoder Management
//----------------------
avif_decoder avif_decoder_create(const opencv_mat buf) {
    auto cvMat = static_cast<const cv::Mat*>(buf);
    if (!cvMat || cvMat->empty()) {
        return nullptr;
    }

    auto d = new avif_decoder_struct();
    memset(d, 0, sizeof(avif_decoder_struct));

    d->buffer = cvMat->data;
    d->buffer_size = cvMat->total();
    d->decoder = avifDecoderCreate();
    
    if (!d->decoder) {
        delete d;
        return nullptr;
    }

    // Enable strict mode for better compatibility
    d->decoder->strictFlags = AVIF_STRICT_ENABLED;

    // Parse the AVIF data
    avifResult result = avifDecoderSetIOMemory(d->decoder, d->buffer, d->buffer_size);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(d->decoder);
        delete d;
        return nullptr;
    }

    result = avifDecoderParse(d->decoder);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(d->decoder);
        delete d;
        return nullptr;
    }

    d->frame_count = d->decoder->imageCount;
    d->total_duration = (d->frame_count > 1) ? 
        (int)((double)d->decoder->durationInTimescales * 1000.0 / d->decoder->timescale) : 0;

    // Read the first frame
    result = avifDecoderNextImage(d->decoder);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(d->decoder);
        delete d;
        return nullptr;
    }

    // Initialize RGB image
    avifRGBImageSetDefaults(&d->rgb, d->decoder->image);
    d->rgb.format = AVIF_RGB_FORMAT_BGR;
    d->rgb.depth = 8;
    
    d->has_alpha = d->decoder->image->alphaPlane != nullptr;
    if (d->has_alpha) {
        d->rgb.format = AVIF_RGB_FORMAT_BGRA;
    }

    result = avifRGBImageAllocatePixels(&d->rgb);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(d->decoder);
        delete d;
        return nullptr;
    }

    d->current_frame = 0;
    d->bgcolor = DEFAULT_BACKGROUND_COLOR;
    d->timescale = 1000;

    return d;
}

void avif_decoder_release(avif_decoder d) {
    if (d) {
        if (d->decoder) {
            avifRGBImageFreePixels(&d->rgb);
            avifDecoderDestroy(d->decoder);
        }
        delete d;
    }
}

//----------------------
// Decoder Properties
//----------------------
int avif_decoder_get_width(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    return d->decoder->image->width;
}

int avif_decoder_get_height(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    return d->decoder->image->height;
}

int avif_decoder_get_pixel_type(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    return d->has_alpha ? CV_8UC4 : CV_8UC3;
}

bool avif_decoder_is_animated(const avif_decoder d) {
    if (!d || !d->decoder) {
        return false;
    }
    return d->frame_count > 1;
}

int avif_decoder_get_frame_count(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    return d->frame_count;
}

int avif_decoder_get_num_frames(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    return d->frame_count;
}

uint32_t avif_decoder_get_duration(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    // Get total duration in milliseconds
    return (uint32_t)(d->decoder->duration * 1000.0f);
}

uint32_t avif_decoder_get_loop_count(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    switch (d->decoder->repetitionCount) {
        case AVIF_REPETITION_COUNT_INFINITE:
        case AVIF_REPETITION_COUNT_UNKNOWN:
            return 0;
        default:
            return d->decoder->repetitionCount;
    }
}

size_t avif_decoder_get_icc(const avif_decoder d, void* buf, size_t buf_len) {
    if (d->decoder->image->icc.size > 0 && d->decoder->image->icc.size <= buf_len) {
        memcpy(buf, d->decoder->image->icc.data, d->decoder->image->icc.size);
        return d->decoder->image->icc.size;
    }
    return 0;
}

uint32_t avif_decoder_get_bg_color(const avif_decoder d) {
    if (!d || !d->decoder) {
        return DEFAULT_BACKGROUND_COLOR;
    }
    return d->bgcolor;
}

int avif_decoder_get_total_duration(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    return d->total_duration;
}

//----------------------
// Frame Properties
//----------------------
int avif_decoder_get_frame_duration(const avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    // Get current frame duration in milliseconds
    return (int)(d->decoder->imageTiming.duration * 1000.0f);
}

int avif_decoder_get_frame_dispose(const avif_decoder d) {
    if (!d || !d->decoder || !d->decoder->image) {
        return 0;
    }
    return d->decoder->image->imageOwnsYUVPlanes ? AVIF_DISPOSE_BACKGROUND : AVIF_DISPOSE_NONE;
}

int avif_decoder_get_frame_blend(const avif_decoder d) {
    if (!d || !d->decoder || !d->decoder->image) {
        return AVIF_BLEND_NONE;
    }
    return d->has_alpha ? AVIF_BLEND_ALPHA : AVIF_BLEND_NONE;
}

int avif_decoder_get_frame_x_offset(const avif_decoder d) {
    if (!d || !d->decoder || !d->decoder->image) {
        return 0;
    }
    // Get horizontal offset from Clean Aperture Box
    if (d->decoder->image->transformFlags & AVIF_TRANSFORM_CLAP) {
        return (int)(d->decoder->image->clap.horizOffN / d->decoder->image->clap.horizOffD);
    }
    return 0;
}

int avif_decoder_get_frame_y_offset(const avif_decoder d) {
    if (!d || !d->decoder || !d->decoder->image) {
        return 0;
    }
    // Get vertical offset from Clean Aperture Box
    if (d->decoder->image->transformFlags & AVIF_TRANSFORM_CLAP) {
        return (int)(d->decoder->image->clap.vertOffN / d->decoder->image->clap.vertOffD);
    }
    return 0;
}

//----------------------
// Frame Operations
//----------------------
bool avif_decoder_decode(avif_decoder d, opencv_mat mat) {
    if (!d || !d->decoder) {
        fprintf(stderr, "Decoder null check failed\n");
        return false;
    }

    // Check if we've already decoded all frames
    if (!avif_decoder_has_more_frames(d)) {
        fprintf(stderr, "EOF: All frames have been decoded\n");
        return false;
    }

    // Convert current frame to RGB/RGBA
    avifResult result = avifImageYUVToRGB(d->decoder->image, &d->rgb);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "YUV to RGB conversion failed for frame %d with error: %s\n", 
               d->current_frame, avifResultToString(result));
        return false;
    }

    // Create OpenCV matrix from AVIF buffer
    auto cvMat = static_cast<cv::Mat*>(mat);
    cv::Mat srcMat(d->rgb.height, d->rgb.width, 
                   d->has_alpha ? CV_8UC4 : CV_8UC3, 
                   d->rgb.pixels, 
                   d->rgb.rowBytes);
    
    // Keep BGR/BGRA format
    if (d->has_alpha) {
        // Direct copy since it's already in BGRA format
        srcMat.copyTo(*cvMat);
    } else {
        // For non-alpha images, just add alpha channel to BGR
        cv::Mat alpha(srcMat.rows, srcMat.cols, CV_8UC1, cv::Scalar(255));
        std::vector<cv::Mat> channels;
        cv::split(srcMat, channels);
        channels.push_back(alpha);
        cv::merge(channels, *cvMat);
    }

    // Advance to next frame if there are more frames
    if (d->current_frame < d->frame_count - 1) {
        // Free current RGB pixels before moving to next frame
        avifRGBImageFreePixels(&d->rgb);
        
        result = avifDecoderNextImage(d->decoder);
        if (result != AVIF_RESULT_OK) {
            fprintf(stderr, "Failed to advance to next frame: %s\n", avifResultToString(result));
            return false;
        }
        
        // Reinitialize RGB image for the new frame
        avifRGBImageSetDefaults(&d->rgb, d->decoder->image);
        d->rgb.format = d->has_alpha ? AVIF_RGB_FORMAT_BGRA : AVIF_RGB_FORMAT_BGR;
        d->rgb.depth = 8;
        
        // Reallocate pixels for the new frame
        result = avifRGBImageAllocatePixels(&d->rgb);
        if (result != AVIF_RESULT_OK) {
            fprintf(stderr, "Failed to allocate RGB pixels for frame %d: %s\n", 
                   d->current_frame, avifResultToString(result));
            return false;
        }
    }
    d->current_frame++;
    return true;
}

int avif_decoder_has_more_frames(avif_decoder d) {
    if (!d || !d->decoder) {
        return 0;
    }
    return (d->current_frame < d->frame_count);
}

//----------------------
// Encoder Management
//----------------------
avif_encoder avif_encoder_create(void* buf, size_t buf_len, const void* icc, size_t icc_len, int loop_count) {
    auto e = new avif_encoder_struct();
    memset(e, 0, sizeof(avif_encoder_struct));

    e->encoder = avifEncoderCreate();
    if (!e->encoder) {
        delete e;
        return nullptr;
    }

    e->dst = static_cast<uint8_t*>(buf);
    e->dst_len = buf_len;
    
    if (icc && icc_len > 0) {
        e->icc = static_cast<const uint8_t*>(icc);
        e->icc_len = icc_len;
    }

    // Configure encoder for animation support
    e->encoder->maxThreads = 1;  // Ensure thread-safe encoding
    e->encoder->repetitionCount = loop_count == 0 ? AVIF_REPETITION_COUNT_INFINITE : loop_count;
    e->encoder->quality = 60;    // Default quality
    e->encoder->timescale = 1000;  // Use milliseconds as timescale (1000 ticks per second)
    e->encoder->speed = AVIF_SPEED_DEFAULT;
    e->encoder->keyframeInterval = 0; // Let encoder decide keyframes
    e->encoder->minQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    e->encoder->maxQuantizer = AVIF_QUANTIZER_WORST_QUALITY;

    return e;
}

void avif_encoder_release(avif_encoder e) {
    if (e) {
        if (e->encoder) {
            avifEncoderDestroy(e->encoder);
        }
        delete e;
    }
}

//----------------------
// Encoder Operations
//----------------------
size_t avif_encoder_write(avif_encoder e, const opencv_mat src, const int* opt, size_t opt_len, int delay_ms, int blend, int dispose) {
    if (!e || !e->encoder) {
        fprintf(stderr, "AVIF Encoder: null check failed\n");
        return 0;
    }

    // Handle flush case
    if (!src) {
        avifRWData output = AVIF_DATA_EMPTY;
        avifResult result = avifEncoderFinish(e->encoder, &output);
        
        if (result != AVIF_RESULT_OK || output.size == 0) {
            fprintf(stderr, "AVIF Encoder: flush failed with error: %s\n", avifResultToString(result));
            avifRWDataFree(&output);
            return 0;
        }

        if (output.size <= e->dst_len) {
            memcpy(e->dst, output.data, output.size);
            size_t size = output.size;
            avifRWDataFree(&output);
            return size;
        }

        avifRWDataFree(&output);
        return 0;
    }

    auto cvMat = static_cast<const cv::Mat*>(src);
    if (!cvMat || cvMat->empty()) {
        fprintf(stderr, "AVIF Encoder: invalid source matrix\n");
        return 0;
    }

    // Create AVIF image
    avifImage* avifImage = avifImageCreate(cvMat->cols, cvMat->rows, 8, AVIF_PIXEL_FORMAT_YUV444);
    if (!avifImage) {
        fprintf(stderr, "AVIF Encoder: failed to create image\n");
        return 0;
    }

    // Set ICC profile if available (only on first frame)
    if (e->icc && e->icc_len > 0 && e->frame_count == 0) {
        avifResult result = avifImageSetProfileICC(avifImage, e->icc, e->icc_len);
        if (result != AVIF_RESULT_OK) {
            fprintf(stderr, "AVIF Encoder: failed to set ICC profile: %s\n", avifResultToString(result));
            return 0;
        }
    }

    // Set encoding options
    for (size_t i = 0; i + 1 < opt_len; i += 2) {
        if (opt[i] == AVIF_QUALITY) {
            e->encoder->quality = std::min(100, std::max(0, opt[i + 1]));
        } else if (opt[i] == AVIF_SPEED) {
            e->encoder->speed = std::min(10, std::max(0, opt[i + 1]));
        }
    }

    // Convert from BGR/BGRA to YUV
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, avifImage);
    
    // Set the correct pixel format based on input channels
    rgb.format = cvMat->channels() == 4 ? AVIF_RGB_FORMAT_BGRA : AVIF_RGB_FORMAT_BGR;
    rgb.depth = 8;
    rgb.pixels = cvMat->data;
    rgb.rowBytes = cvMat->step;
    rgb.width = cvMat->cols;
    rgb.height = cvMat->rows;

    avifResult result = avifImageRGBToYUV(avifImage, &rgb);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "AVIF Encoder: RGB to YUV conversion failed: %s\n", avifResultToString(result));
        avifImageDestroy(avifImage);
        return 0;
    }

    // Set up frame timing and flags
    float durationInTimescale = (float)delay_ms * e->encoder->timescale;
    if (durationInTimescale < e->encoder->timescale) {
        durationInTimescale = e->encoder->timescale;  // Ensure minimum duration
    }
    float durationInSeconds = durationInTimescale / e->encoder->timescale;

    // Handle blending mode
    avifAddImageFlags flags = AVIF_ADD_IMAGE_FLAG_NONE;
    if (blend == 1) { // BLEND_OVER
        flags |= AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME;
    }

    // Add frame to encoder
    result = avifEncoderAddImage(e->encoder, avifImage, durationInSeconds, flags);
    avifImageDestroy(avifImage);

    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "AVIF Encoder: failed to add frame: %s\n", avifResultToString(result));
        return 0;
    }

    e->frame_count++;
    return 1; // Return success without actual data (data comes in flush)
}

size_t avif_encoder_flush(avif_encoder e) {
    return avif_encoder_write(e, nullptr, nullptr, 0, 0, 0, 0);
}
