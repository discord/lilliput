#include "tone_mapping.hpp"
#include <opencv2/opencv.hpp>
#include <lcms2.h>
#include <cstring>
#include <memory>
#include <algorithm>

// Tone mapping constants
constexpr float MIN_LUMA_THRESHOLD = 0.001f;  // Threshold to avoid division by near-zero luminance
constexpr float REINHARD_LUMINANCE_SCALE = 0.85f;  // Moderate compression for HDR content

// Helper function to calculate luminance from RGB using Rec.709 coefficients
static inline float calculate_luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/**
 * Applies HDR to SDR tone mapping using Reinhard algorithm.
 *
 * This function detects PQ (Perceptual Quantizer) HDR profiles and applies
 * luminance-based tone mapping to reduce brightness while preserving color
 * relationships. Non-PQ images are returned unchanged (as a copy).
 *
 * @param src Source image (BGR or BGRA format, 8-bit)
 * @param icc_data ICC profile data from the source image
 * @param icc_len Length of ICC profile data in bytes
 * @return Tone-mapped image (caller owns the pointer), or nullptr on error
 */
cv::Mat* apply_hdr_to_sdr_tone_mapping(
    const cv::Mat* src,
    const uint8_t* icc_data,
    size_t icc_len
)
{
    if (!src || !icc_data || icc_len == 0) {
        return nullptr;
    }

    // Only support 8-bit RGB or RGBA
    int channels = src->channels();
    if (src->depth() != CV_8U || (channels != 3 && channels != 4)) {
        return nullptr;
    }

    // Load ICC profile
    // NOTE: it may not be reliable to use just the icc profile for detecting HDR. some formats contain
    // flags and not profiles, but going to go with this for a first pass
    cmsHPROFILE src_profile = cmsOpenProfileFromMem(icc_data, icc_len);
    if (!src_profile) {
        return nullptr;
    }

    // Check if this is a PQ/HDR profile
    char profile_desc[256] = {0};
    cmsGetProfileInfoASCII(src_profile, cmsInfoDescription, "en", "US", profile_desc, sizeof(profile_desc));

    bool is_pq_profile = (strstr(profile_desc, "PQ") != nullptr ||
                          strstr(profile_desc, "2100") != nullptr ||
                          strstr(profile_desc, "2020") != nullptr);

    // Done with profile, we only needed it for PQ detection
    cmsCloseProfile(src_profile);

    // If not PQ, just return a copy unchanged
    if (!is_pq_profile) {
        return new cv::Mat(*src);
    }

    // Handle alpha channel separately - alpha should NOT be tone mapped
    bool has_alpha = (channels == 4);
    std::unique_ptr<cv::Mat> bgr_only;
    std::unique_ptr<cv::Mat> alpha_channel;
    const cv::Mat* src_for_transform = src;

    if (has_alpha) {
        bgr_only = std::make_unique<cv::Mat>(src->rows, src->cols, CV_8UC3);
        alpha_channel = std::make_unique<cv::Mat>(src->rows, src->cols, CV_8UC1);

        cv::Mat channels_split[4];
        cv::split(*src, channels_split);

        cv::Mat bgr_channels[3] = {channels_split[0], channels_split[1], channels_split[2]};
        cv::merge(bgr_channels, 3, *bgr_only);
        *alpha_channel = channels_split[3];

        src_for_transform = bgr_only.get();
    }

    // Analyze image brightness to adaptively tune tone mapping scale
    // Calculate average luminance across the image
    float total_luma = 0.0f;
    int pixel_count = src_for_transform->rows * src_for_transform->cols;

    for (int y = 0; y < src_for_transform->rows; y++) {
        const uint8_t* src_row = src_for_transform->ptr<uint8_t>(y);
        for (int x = 0; x < src_for_transform->cols; x++) {
            int idx = x * 3;
            float b = src_row[idx + 0] / 255.0f;
            float g = src_row[idx + 1] / 255.0f;
            float r = src_row[idx + 2] / 255.0f;
            total_luma += calculate_luminance(r, g, b);
        }
    }
    float avg_brightness = total_luma / pixel_count;

    // Adaptive scale factor: brighter images get more compression (lower scale)
    // Map brightness [0.0-1.0] to scale [0.85-1.1]
    // Very bright images (0.7+) get compression (0.85-0.92)
    // Moderate images (0.3-0.7) get balanced treatment (0.92-1.02)
    // Dark images (0.0-0.3) get slight boost (1.02-1.1)
    float adaptive_scale = 1.1f - (avg_brightness * 0.25f);
    adaptive_scale = std::max(0.85f, std::min(1.1f, adaptive_scale));

    // Apply Reinhard tone mapping
    // Tried to use OpenCV's built in tone mapping, but ran into issues with
    // dimming blown out/deep fried images. Using this as a first pass
    std::unique_ptr<cv::Mat> dst_bgr = std::make_unique<cv::Mat>(src_for_transform->rows, src_for_transform->cols, CV_8UC3);

    // Apply luminance-based tone mapping to preserve color relationships
    // This prevents oversaturation by operating on brightness only
    for (int y = 0; y < src_for_transform->rows; y++) {
        const uint8_t* src_row = src_for_transform->ptr<uint8_t>(y);
        uint8_t* dst_row = dst_bgr->ptr<uint8_t>(y);

        for (int x = 0; x < src_for_transform->cols; x++) {
            int idx = x * 3;
            // BGR order
            float b = src_row[idx + 0] / 255.0f;
            float g = src_row[idx + 1] / 255.0f;
            float r = src_row[idx + 2] / 255.0f;

            // Calculate luminance using Rec.709 coefficients
            float luma = calculate_luminance(r, g, b);

            // Apply Reinhard tone mapping to luminance only with adaptive scale
            float luma_scaled = luma * adaptive_scale;
            float luma_mapped = luma_scaled / (1.0f + luma_scaled);

            // Scale RGB channels by the luminance ratio to preserve color
            float ratio = (luma > MIN_LUMA_THRESHOLD) ? (luma_mapped / luma) : 0.0f;

            dst_row[idx + 0] = static_cast<uint8_t>(std::min(b * ratio * 255.0f, 255.0f));
            dst_row[idx + 1] = static_cast<uint8_t>(std::min(g * ratio * 255.0f, 255.0f));
            dst_row[idx + 2] = static_cast<uint8_t>(std::min(r * ratio * 255.0f, 255.0f));
        }
    }

    if (has_alpha) {
        auto result = std::make_unique<cv::Mat>(src->rows, src->cols, src->type());
        cv::Mat bgr_channels_out[3];
        cv::split(*dst_bgr, bgr_channels_out);
        cv::Mat final_channels[4] = {bgr_channels_out[0], bgr_channels_out[1], bgr_channels_out[2], *alpha_channel};
        cv::merge(final_channels, 4, *result);
        return result.release();
    } else {
        return dst_bgr.release();
    }
}

// C FFI wrapper for tone mapping
extern "C" {

opencv_mat apply_tone_mapping_ffi(
    const opencv_mat src,
    const uint8_t* icc_data,
    size_t icc_len
)
{
    auto mat = static_cast<const cv::Mat*>(src);
    if (!mat || mat->empty()) {
        return nullptr;
    }

    if (!icc_data || icc_len == 0) {
        // No ICC profile, just return a copy
        return new cv::Mat(*mat);
    }

    return apply_hdr_to_sdr_tone_mapping(mat, icc_data, icc_len);
}

}
