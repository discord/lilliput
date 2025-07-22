#include "avif.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <avif/avif.h>
#include <lcms2.h>
#include <cstring>
#include "icc_profiles/rec709_profile.h"


//--------------------------------
// Constants
//--------------------------------

// Default background color for AVIF images (white, fully opaque)
#define DEFAULT_BACKGROUND_COLOR 0xFFFFFFFF

//----------------------
// Types and Structures
//----------------------
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

/**
 * Handles errors from the LCMS library.
 * @param ContextID The context ID.
 * @param ErrorCode The error code.
 * @param Text The error text.
 */
static void avif_cms_error_handler(cmsContext ContextID, cmsUInt32Number ErrorCode, const char *Text)
{
    fprintf(stderr, "LCMS error: %s (ErrorCode: %d)\n", Text, ErrorCode);
}

/**
 * Gets the color information from the AVIF image.
 * @param image The AVIF image.
 * @param colorPrimaries The color primaries.
 * @param transferCharacteristics The transfer characteristics.
 */
static void avif_get_color_info(const avifImage* image, avifColorPrimaries* colorPrimaries, avifTransferCharacteristics* transferCharacteristics)
{
    *colorPrimaries = image->colorPrimaries;
    *transferCharacteristics = image->transferCharacteristics;

    if (image->icc.data && image->icc.size > 0) {
        cmsContext ctx = cmsCreateContext(NULL, NULL);
        cmsSetLogErrorHandler(avif_cms_error_handler);
        
        cmsHPROFILE profile = cmsOpenProfileFromMem(image->icc.data, image->icc.size);
        if (profile) {
            cmsVideoSignalType* cicp = (cmsVideoSignalType*)cmsReadTag(profile, cmsSigcicpTag);
            if (cicp) {
                if (cicp->ColourPrimaries != AVIF_COLOR_PRIMARIES_UNSPECIFIED) {
                    *colorPrimaries = cicp->ColourPrimaries;
                }
                if (cicp->TransferCharacteristics != AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED) {
                    *transferCharacteristics = cicp->TransferCharacteristics;
                }
            }
            cmsCloseProfile(profile);
        }
        cmsDeleteContext(ctx);
    }
}

/**
 * Checks if the AVIF image is a HDR source.
 * @param image The AVIF image.
 * @return True if the AVIF image is a HDR source, false otherwise.
 */
static bool avif_is_hdr_source(const avifImage* image)
{
    if (!image)
        return false;

    avifColorPrimaries colorPrimaries;
    avifTransferCharacteristics transferCharacteristics;
    avif_get_color_info(image, &colorPrimaries, &transferCharacteristics);

    bool hdr_primaries = colorPrimaries == AVIF_COLOR_PRIMARIES_BT2020;
    bool hdr_transfer = (transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_PQ) ||
                       (transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_HLG);

    bool high_bit_depth = image->depth > 8;
    return high_bit_depth && (hdr_primaries || hdr_transfer);
}

/**
 * Converts PQ (SMPTE ST.2084) to linear.
 * @param x The input value.
 * @return The linear value.
 */
static float avif_pq_to_linear(float x)
{
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

/**
 * Converts HLG to linear.
 * @param x The input value.
 * @return The linear value.
 */
static float avif_hlg_to_linear(float x)
{
    const float a = 0.17883277;
    const float b = 0.28466892;
    const float c = 0.55991073;

    if (x <= 0.5f) {
        return x * x / 3.0f;
    }
    else {
        return (std::exp((x - c) / a) + b) / 12.0f;
    }
}

/**
 * Converts HDR RGB values to SDR using OpenCV's tone-mapping.
 * @param src The source image.
 * @param dst The destination image.
 * @param width The width of the image.
 * @param height The height of the image.
 * @param src_depth The depth of the source image.
 * @param transfer The transfer characteristics.
 * @param primaries The color primaries.
 */
static void avif_tonemap_rgb(uint16_t* src,
                             uint8_t* dst,
                             int width,
                             int height,
                             int src_depth,
                             avifTransferCharacteristics transfer,
                             avifColorPrimaries primaries)
{
    float scale = 1.0f / ((1 << src_depth) - 1);

    // Create OpenCV matrices for processing
    cv::Mat hdrMat(height, width, CV_32FC3);
    cv::Mat sdrMat(height, width, CV_8UC3);

    // Convert to linear RGB
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            float r = src[idx] * scale;
            float g = src[idx + 1] * scale;
            float b = src[idx + 2] * scale;

            // Convert to linear
            if (transfer == AVIF_TRANSFER_CHARACTERISTICS_PQ) {
                r = avif_pq_to_linear(r);
                g = avif_pq_to_linear(g);
                b = avif_pq_to_linear(b);
            }
            else if (transfer == AVIF_TRANSFER_CHARACTERISTICS_HLG) {
                r = avif_hlg_to_linear(r);
                g = avif_hlg_to_linear(g);
                b = avif_hlg_to_linear(b);
            }

            hdrMat.at<cv::Vec3f>(y, x) = cv::Vec3f(r, g, b);
        }
    }

    // Create a Reinhard tonemap with typical parameters for HDR content
    cv::Ptr<cv::TonemapReinhard> tonemap = cv::createTonemapReinhard(1.0f, 0.6f, 0.2f, 0.3f);
    cv::Mat tonemapped;
    tonemap->process(hdrMat, tonemapped);

    // Convert colorspace if needed
    cv::Mat converted;
    if (primaries == AVIF_COLOR_PRIMARIES_BT2020) {
        cv::Matx33f bt2020_to_bt709(
          1.6605f, -0.5876f, -0.0728f, -0.1246f, 1.1329f, -0.0083f, -0.0182f, -0.1006f, 1.1187f);
        cv::transform(tonemapped, converted, bt2020_to_bt709);
    }
    else if (primaries == AVIF_COLOR_PRIMARIES_SMPTE432 ||
             primaries == AVIF_COLOR_PRIMARIES_DCI_P3) {
        cv::Matx33f p3_to_bt709(
          1.2249f, -0.2247f, -0.0002f, -0.0420f, 1.0419f, 0.0001f, -0.0197f, 0.0754f, 0.9443f);
        cv::transform(tonemapped, converted, p3_to_bt709);
    }
    else if (primaries == AVIF_COLOR_PRIMARIES_BT601) {
        cv::Matx33f bt601_to_bt709(
          1.0440f, -0.0440f, 0.0000f, -0.0000f, 1.0000f, 0.0000f, 0.0000f, 0.0000f, 1.0000f);
        cv::transform(tonemapped, converted, bt601_to_bt709);
    }
    else {
        // For unknown colorspaces, default to assuming BT709
        converted = tonemapped;
    }

    // Convert to 8-bit with proper gamma correction
    cv::Mat gamma_corrected;
    if (transfer == AVIF_TRANSFER_CHARACTERISTICS_LINEAR) {
        cv::pow(converted, 1.0f / 2.2f, gamma_corrected);
    }
    else {
        // PQ and HLG already include transfer function
        gamma_corrected = converted;
    }
    gamma_corrected.convertTo(sdrMat, CV_8UC3, 255.0f);

    memcpy(dst, sdrMat.data, width * height * 3);
}

/**
 * Converts YUV to RGB with optional HDR tone-mapping.
 * @param image The AVIF image.
 * @param rgb The RGB image.
 * @param enable_tone_mapping Whether to enable tone-mapping.
 * @return The result of the conversion.
 */
static avifResult avif_convert_yuv_to_rgb_with_tone_mapping(avifImage* image,
                                                            avifRGBImage* rgb,
                                                            bool enable_tone_mapping)
{
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

    avifColorPrimaries colorPrimaries;
    avifTransferCharacteristics transferCharacteristics;
    avif_get_color_info(image, &colorPrimaries, &transferCharacteristics);

    // Apply tone-mapping with colorspace information
    avif_tonemap_rgb((uint16_t*)temp.pixels,
                     rgb->pixels,
                     image->width,
                     image->height,
                     temp.depth,
                     transferCharacteristics,
                     colorPrimaries);

    avifRGBImageFreePixels(&temp);
    return AVIF_RESULT_OK;
}

//----------------------
// Decoder Management
//----------------------

/**
 * Creates an AVIF decoder.
 * @param buf The input buffer.
 * @param tone_mapping_enabled Whether to enable tone-mapping.
 * @return The AVIF decoder.
 */
avif_decoder avif_decoder_create(const opencv_mat buf, const bool tone_mapping_enabled)
{
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
    d->total_duration = (d->frame_count > 1)
      ? (int)((double)d->decoder->durationInTimescales * 1000.0 / d->decoder->timescale)
      : 0;

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

/**
 * Releases the AVIF decoder.
 * @param d The AVIF decoder.
 */
void avif_decoder_release(avif_decoder d)
{
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

/**
 * Gets the width of the AVIF image.
 * @param d The AVIF decoder.
 * @return The width of the AVIF image.
 */
int avif_decoder_get_width(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    return d->decoder->image->width;
}

/**
 * Gets the height of the AVIF image.
 * @param d The AVIF decoder.
 * @return The height of the AVIF image.
 */
int avif_decoder_get_height(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    return d->decoder->image->height;
}

/**
 * Gets the pixel type of the AVIF image.
 * @param d The AVIF decoder.
 * @return The pixel type of the AVIF image.
 */
int avif_decoder_get_pixel_type(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    return d->has_alpha ? CV_8UC4 : CV_8UC3;
}

/**
 * Checks if the AVIF image is animated.
 * @param d The AVIF decoder.
 * @return True if the AVIF image is animated, false otherwise.
 */
bool avif_decoder_is_animated(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return false;
    }
    return d->frame_count > 1;
}

/**
 * Gets the frame count of the AVIF image.
 * @param d The AVIF decoder.
 * @return The frame count of the AVIF image.
 */
int avif_decoder_get_frame_count(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    return d->frame_count;
}

/**
 * Gets the number of frames in the AVIF image.
 * @param d The AVIF decoder.
 * @return The number of frames in the AVIF image.
 */
int avif_decoder_get_num_frames(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    return d->frame_count;
}

/**
 * Gets the duration of the AVIF image.
 * @param d The AVIF decoder.
 * @return The duration of the AVIF image.
 */
uint32_t avif_decoder_get_duration(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    // Get total duration in milliseconds
    return (uint32_t)(d->decoder->duration * 1000.0f);
}

/**
 * Gets the loop count of the AVIF image.
 * @param d The AVIF decoder.
 * @return The loop count of the AVIF image.
 */
uint32_t avif_decoder_get_loop_count(const avif_decoder d)
{
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

/**
 * Gets the ICC profile of the AVIF image.
 * @param d The AVIF decoder.
 * @param buf The buffer to store the ICC profile.
 * @param buf_len The length of the buffer.
 * @return The ICC profile of the AVIF image.
 */
size_t avif_decoder_get_icc(const avif_decoder d, void* buf, size_t buf_len)
{
    if (!d || !d->decoder) {
        return 0;
    }

    // Report rec709 profile for tone-mapped HDR content
    if (d->tone_mapping_enabled && avif_is_hdr_source(d->decoder->image)) {
        size_t profile_size = sizeof(rec709_profile);
        const uint8_t* profile_data = rec709_profile;
        std::memcpy(buf, profile_data, profile_size);
        return static_cast<int>(profile_size);
    }

    // Always preserve ICC profile for HDR content, even when tone mapping
    // This ensures proper colorspace information is maintained
    if (d->decoder->image->icc.size > 0 && d->decoder->image->icc.size <= buf_len) {
        memcpy(buf, d->decoder->image->icc.data, d->decoder->image->icc.size);
        return d->decoder->image->icc.size;
    }
    return 0;
}

/**
 * Gets the background color of the AVIF image.
 * @param d The AVIF decoder.
 * @return The background color of the AVIF image.
 */
uint32_t avif_decoder_get_bg_color(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return DEFAULT_BACKGROUND_COLOR;
    }
    return d->bgcolor;
}

/**
 * Gets the total duration of the AVIF image.
 * @param d The AVIF decoder.
 * @return The total duration of the AVIF image.
 */
int avif_decoder_get_total_duration(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    return d->total_duration;
}

//----------------------
// Frame Properties
//----------------------

/**
 * Gets the duration of the current frame.
 * @param d The AVIF decoder.
 * @return The duration of the current frame.
 */
int avif_decoder_get_frame_duration(const avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    // Get current frame duration in milliseconds
    return (int)(d->decoder->imageTiming.duration * 1000.0f);
}

/**
 * Gets the disposal method for the current frame.
 * @param d The AVIF decoder.
 * @return The disposal method for the current frame.
 */
int avif_decoder_get_frame_dispose(const avif_decoder d)
{
    if (!d || !d->decoder || !d->decoder->image) {
        return 0;
    }
    
    if (avif_decoder_is_animated(d)) {
        if (d->has_alpha && d->decoder->image->alphaPremultiplied) {
            return AVIF_DISPOSE_NONE;
        }
        return AVIF_DISPOSE_BACKGROUND;
    }
    
    // For non-animated images, use a simpler heuristic
    return d->decoder->image->imageOwnsYUVPlanes ? AVIF_DISPOSE_BACKGROUND : AVIF_DISPOSE_NONE;
}

/**
 * Gets the blend method for the current frame.
 * @param d The AVIF decoder.
 * @return The blend method for the current frame.
 */
int avif_decoder_get_frame_blend(const avif_decoder d)
{
    if (!d || !d->decoder || !d->decoder->image) {
        return AVIF_BLEND_NONE;
    }

    if (avif_decoder_is_animated(d)) {
        if (d->has_alpha && (d->decoder->image->alphaPremultiplied || d->decoder->image->alphaPlane)) { 
            return AVIF_BLEND_ALPHA;
        }
        return AVIF_BLEND_NONE;
    }
    
    // For non-animated images, use alpha status to determine blend method
    return d->has_alpha ? AVIF_BLEND_ALPHA : AVIF_BLEND_NONE;
}

/**
 * Gets the horizontal offset of the current frame.
 * @param d The AVIF decoder.
 * @return The horizontal offset of the current frame.
 */
int avif_decoder_get_frame_x_offset(const avif_decoder d)
{
    if (!d || !d->decoder || !d->decoder->image) {
        return 0;
    }
    // Get horizontal offset from Clean Aperture Box
    if (d->decoder->image->transformFlags & AVIF_TRANSFORM_CLAP) {
        return (int)(d->decoder->image->clap.horizOffN / d->decoder->image->clap.horizOffD);
    }
    return 0;
}

/**
 * Gets the vertical offset of the current frame.
 * @param d The AVIF decoder.
 * @return The vertical offset of the current frame.
 */
int avif_decoder_get_frame_y_offset(const avif_decoder d)
{
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

/**
 * Decodes the AVIF image.
 * @param d The AVIF decoder.
 * @param mat The OpenCV matrix to copy the frame to.
 * @return True if the decode operation was successful, false otherwise.
 */
bool avif_decoder_decode(avif_decoder d, opencv_mat mat)
{
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
    avifResult result = avif_convert_yuv_to_rgb_with_tone_mapping(
      d->decoder->image, &d->rgb, d->tone_mapping_enabled);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr,
                "YUV to RGB conversion failed for frame %d: %s\n",
                d->current_frame,
                avifResultToString(result));
        return false;
    }

    // Create OpenCV matrix from AVIF buffer
    auto cvMat = static_cast<cv::Mat*>(mat);
    cv::Mat srcMat(d->rgb.height,
                   d->rgb.width,
                   d->has_alpha ? CV_8UC4 : CV_8UC3,
                   d->rgb.pixels,
                   d->rgb.rowBytes);

    // Keep BGR/BGRA format
    if (d->has_alpha) {
        // Direct copy since it's already in BGRA format
        srcMat.copyTo(*cvMat);
    }
    else {
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
            fprintf(stderr,
                    "Failed to allocate RGB pixels for frame %d: %s\n",
                    d->current_frame,
                    avifResultToString(result));
            return false;
        }
    }
    d->current_frame++;
    return true;
}

/**
 * Checks if there are more frames to decode.
 * @param d The AVIF decoder.
 * @return True if there are more frames to decode, false otherwise.
 */
int avif_decoder_has_more_frames(avif_decoder d)
{
    if (!d || !d->decoder) {
        return 0;
    }
    return (d->current_frame < d->frame_count);
}

//----------------------
// Encoder Management
//----------------------

/**
 * Creates an AVIF encoder.
 * @param buf The output buffer.
 * @param buf_len The length of the output buffer.
 * @param icc The ICC profile.
 * @param icc_len The length of the ICC profile.
 * @param loop_count The loop count.
 * @return The AVIF encoder.
 */
avif_encoder avif_encoder_create(void* buf,
                                 size_t buf_len,
                                 const void* icc,
                                 size_t icc_len,
                                 int loop_count)
{
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
    e->encoder->maxThreads = 1; // Ensure thread-safe encoding
    e->encoder->repetitionCount = loop_count == 0 ? AVIF_REPETITION_COUNT_INFINITE : loop_count;
    e->encoder->quality = 60;     // Default quality
    e->encoder->timescale = 1000; // Use milliseconds as timescale (1000 ticks per second)
    e->encoder->speed = AVIF_SPEED_DEFAULT;
    e->encoder->keyframeInterval = 0; // Let encoder decide keyframes
    e->encoder->minQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    e->encoder->maxQuantizer = AVIF_QUANTIZER_WORST_QUALITY;

    return e;
}

/**
 * Releases the AVIF encoder.
 * @param e The AVIF encoder.
 */
void avif_encoder_release(avif_encoder e)
{
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

/**
 * Writes an AVIF image to the output buffer.
 * @param e The AVIF encoder.
 * @param src The source image.
 * @param opt The options.
 * @param opt_len The length of the options.
 * @param delay_ms The delay in milliseconds.
 * @param blend The blend mode.
 * @param dispose The dispose mode.
 * @return The number of bytes written to the output buffer.
 */
size_t avif_encoder_write(avif_encoder e,
                          const opencv_mat src,
                          const int* opt,
                          size_t opt_len,
                          int delay_ms,
                          int blend,
                          int dispose)
{
    if (!e || !e->encoder) {
        fprintf(stderr, "AVIF Encoder: null check failed\n");
        return 0;
    }

    // Handle flush case
    if (!src) {
        avifRWData output = AVIF_DATA_EMPTY;
        avifResult result = avifEncoderFinish(e->encoder, &output);

        if (result != AVIF_RESULT_OK || output.size == 0) {
            fprintf(
              stderr, "AVIF Encoder: flush failed with error: %s\n", avifResultToString(result));
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
            fprintf(
              stderr, "AVIF Encoder: failed to set ICC profile: %s\n", avifResultToString(result));
            return 0;
        }
    }

    // Set encoding options
    for (size_t i = 0; i + 1 < opt_len; i += 2) {
        if (opt[i] == AVIF_QUALITY) {
            e->encoder->quality = std::min(100, std::max(0, opt[i + 1]));
        }
        else if (opt[i] == AVIF_SPEED) {
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
        fprintf(
          stderr, "AVIF Encoder: RGB to YUV conversion failed: %s\n", avifResultToString(result));
        avifImageDestroy(avifImage);
        return 0;
    }

    // Set up frame timing and flags
    float durationInTimescale = (float)delay_ms * e->encoder->timescale;
    if (durationInTimescale < e->encoder->timescale) {
        durationInTimescale = e->encoder->timescale; // Ensure minimum duration
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

/**
 * Flushes the AVIF encoder.
 * @param e The AVIF encoder.
 * @return The number of bytes written to the output buffer.
 */
size_t avif_encoder_flush(avif_encoder e)
{
    return avif_encoder_write(e, nullptr, nullptr, 0, 0, 0, 0);
}
