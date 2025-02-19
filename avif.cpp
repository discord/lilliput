#include "avif.hpp"
#include <opencv2/imgproc.hpp>
#include <avif/avif.h>
#include <cstring>

#define DEFAULT_BACKGROUND_COLOR 0xFFFFFFFF

//----------------------
// Types and Structures
//----------------------
struct avif_tone_map_params {
    // Increased exposure for brighter overall image
    // Range typically 3.0-5.0 in professional tools
    float exposure = 5.0f;

    // Higher white point to preserve more highlight detail
    // While preventing highlight clipping
    float white_point = 5.5f;

    // Slightly higher contrast to maintain HDR "pop"
    // But not so high it crushes shadows
    float contrast = 1.45f;

    // Slightly adjusted gamma for better shadow detail
    // While maintaining rich midtones
    float gamma = 1.0f / 2.25f;
};

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
    bool tone_mapping_enabled;
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
// HDR Support Functions
//----------------------
// Check if source is HDR based on bit depth, primaries and transfer characteristics
static bool avif_is_hdr_source(const avifImage* image) {
    if (!image) return false;
    
    bool high_bit_depth = image->depth > 8;
    bool hdr_primaries = image->colorPrimaries == AVIF_COLOR_PRIMARIES_BT2020;
    bool hdr_transfer = (image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_PQ) ||
                       (image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_HLG);
    
    return high_bit_depth && (hdr_primaries || hdr_transfer);
}

// Convert PQ (SMPTE ST.2084) to linear
static float avif_pq_to_linear(float x) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    float xpow = std::pow(x, 1.0f / m2);
    float num = std::max(xpow - c1, 0.0f);
    float den = c2 - c3 * xpow;
    float linear = std::pow(num / den, 1.0f / m1);
    
    return linear;
}

// Convert HLG to linear
static float avif_hlg_to_linear(float x) {
    const float a = 0.17883277;
    const float b = 0.28466892;
    const float c = 0.55991073;
    
    if (x <= 0.5f) {
        return x * x / 3.0f;
    } else {
        return (std::exp((x - c) / a) + b) / 12.0f;
    }
}

// Reinhard tone-mapping operator
static float avif_reinhard_tonemap(float x, const avif_tone_map_params& params) {
    // Apply exposure (with HDR-specific boost)
    x *= params.exposure;
    
    // Modified Reinhard tone compression with shoulder
    float compressed = (x * (1.0f + x / (params.white_point * params.white_point))) / (1.0f + x);
    
    // Apply contrast
    if (params.contrast != 1.0f) {
        // Center contrast adjustment around middle gray
        const float mid = 0.18f;
        compressed = mid * std::pow(compressed / mid, params.contrast);
    }
    
    // Apply gamma correction
    if (params.gamma != 1.0f) {
        compressed = std::pow(compressed, params.gamma);
    }
    
    return compressed;
}

// Convert HDR RGB values to SDR using tone-mapping
static void avif_tonemap_rgb(uint16_t* src, uint8_t* dst, int width, int height, int src_depth, 
                            avifTransferCharacteristics transfer, const avif_tone_map_params& params) {
    float scale = 1.0f / ((1 << src_depth) - 1);
    float inv_scale = 255.0f;
    
    // Pre-analyze image for better exposure adjustment
    float max_value = 0.0f;
    float avg_value = 0.0f;
    int num_pixels = width * height * 3;
    
    // Calculate max and average values
    for (int i = 0; i < num_pixels; i++) {
        float x = src[i] * scale;
        if (transfer == AVIF_TRANSFER_CHARACTERISTICS_PQ) {
            x = avif_pq_to_linear(x);
        } else if (transfer == AVIF_TRANSFER_CHARACTERISTICS_HLG) {
            x = avif_hlg_to_linear(x);
        }
        max_value = std::max(max_value, x);
        avg_value += x;
    }
    avg_value /= num_pixels;
    
    // Dynamically adjust exposure based on both max and average values
    avif_tone_map_params adjusted_params = params;
    if (max_value > 0.0f) {
        float target_max = 0.95f;  // Increased from 0.8f for brighter highlights
        float target_avg = 0.35f;  // Target average brightness (increased for overall brightness)
        
        // Calculate separate adjustments for max and average values
        float max_exposure_scale = std::min(3.0f, target_max / max_value);  // Increased limit from 2.0f
        float avg_exposure_scale = target_avg / std::max(avg_value, 1e-6f);
        avg_exposure_scale = std::min(std::max(avg_exposure_scale, 0.8f), 2.5f);  // Wider range
        
        // Blend the two exposure adjustments, favoring the max-based adjustment
        adjusted_params.exposure *= (max_exposure_scale * 0.6f + avg_exposure_scale * 0.4f);
    }
    
    // Enhanced color preservation for brighter results
    for (int i = 0; i < width * height; i++) {
        float r = src[i * 3] * scale;
        float g = src[i * 3 + 1] * scale;
        float b = src[i * 3 + 2] * scale;
        
        // Convert to linear
        if (transfer == AVIF_TRANSFER_CHARACTERISTICS_PQ) {
            r = avif_pq_to_linear(r);
            g = avif_pq_to_linear(g);
            b = avif_pq_to_linear(b);
        } else if (transfer == AVIF_TRANSFER_CHARACTERISTICS_HLG) {
            r = avif_hlg_to_linear(r);
            g = avif_hlg_to_linear(g);
            b = avif_hlg_to_linear(b);
        }
        
        // Calculate luminance
        float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        if (luminance < 1e-6f) {
            dst[i * 3] = dst[i * 3 + 1] = dst[i * 3 + 2] = 0;
            continue;
        }
        
        // Preserve color ratios
        float r_ratio = r / (luminance + 1e-6f);
        float g_ratio = g / (luminance + 1e-6f);
        float b_ratio = b / (luminance + 1e-6f);
        
        // Tone-map luminance
        float mapped_luminance = avif_reinhard_tonemap(luminance, adjusted_params);
        
        // Apply saturation boost for brighter appearance
        float sat_boost = 1.1f + 0.2f * mapped_luminance;  // Increase saturation in brighter areas
        
        // Reconstruct color with enhanced saturation
        r = mapped_luminance * (1.0f + (r_ratio - 1.0f) * sat_boost);
        g = mapped_luminance * (1.0f + (g_ratio - 1.0f) * sat_boost);
        b = mapped_luminance * (1.0f + (b_ratio - 1.0f) * sat_boost);
        
        // Convert back to 8-bit with highlight preservation
        dst[i * 3] = (uint8_t)std::min(std::max(r * inv_scale + 0.5f, 0.0f), 255.0f);
        dst[i * 3 + 1] = (uint8_t)std::min(std::max(g * inv_scale + 0.5f, 0.0f), 255.0f);
        dst[i * 3 + 2] = (uint8_t)std::min(std::max(b * inv_scale + 0.5f, 0.0f), 255.0f);
    }
}

// Convert YUV to RGB with optional HDR tone-mapping
static avifResult avif_convert_yuv_to_rgb_with_tone_mapping(avifImage* image, avifRGBImage* rgb, bool enable_tone_mapping, avif_tone_map_params params = avif_tone_map_params()) {
    if (!enable_tone_mapping || !avif_is_hdr_source(image)) {
        // Not HDR or tone-mapping disabled, proceed with normal YUV to RGB conversion
        return avifImageYUVToRGB(image, rgb);
    }
    
    // For HDR with tone-mapping enabled, we need to:
    // 1. Convert YUV to high bit depth RGB
    // 2. Apply tone-mapping
    // 3. Convert to 8-bit RGB
    
    // First convert YUV to high bit depth RGB
    avifRGBImage temp;
    avifRGBImageSetDefaults(&temp, image);
    temp.depth = image->depth;
    temp.format = rgb->format;
    
    avifResult result = avifRGBImageAllocatePixels(&temp);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to allocate pixels for temp image\n");
        return result;
    }
    
    result = avifImageYUVToRGB(image, &temp);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to convert YUV to RGB\n");
        avifRGBImageFreePixels(&temp);
        return result;
    }
    
    // Apply tone-mapping
    avif_tonemap_rgb((uint16_t*)temp.pixels, rgb->pixels, 
                     image->width, image->height, 
                     temp.depth, image->transferCharacteristics, params);
    
    avifRGBImageFreePixels(&temp);
    return AVIF_RESULT_OK;
}

//----------------------
// Decoder Management
//----------------------
avif_decoder avif_decoder_create(const opencv_mat buf, const bool tone_mapping_enabled) {
    auto cvMat = static_cast<const cv::Mat*>(buf);
    if (!cvMat || cvMat->empty()) {
        return nullptr;
    }

    auto d = new avif_decoder_struct();
    memset(d, 0, sizeof(avif_decoder_struct));

    d->buffer = cvMat->data;
    d->buffer_size = cvMat->total();
    d->decoder = avifDecoderCreate();
    d->tone_mapping_enabled = tone_mapping_enabled;
    
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

    // Convert YUV to RGB with optional HDR handling
    avifResult result = avif_convert_yuv_to_rgb_with_tone_mapping(d->decoder->image, &d->rgb, d->tone_mapping_enabled);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "YUV to RGB conversion failed for frame %d: %s\n", d->current_frame, avifResultToString(result));
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
