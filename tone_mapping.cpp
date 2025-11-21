#include "tone_mapping.hpp"
#include <cstring>
#include <memory>

// Tone mapping constants
constexpr float MIN_LUMA_THRESHOLD = 0.001f;  // Threshold to avoid division by near-zero luminance
constexpr float REINHARD_LUMINANCE_SCALE = 1.2f;  // Gentle luminance boost before tone mapping

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
            float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;

            // Apply gentle Reinhard tone mapping to luminance only
            float luma_scaled = luma * REINHARD_LUMINANCE_SCALE;
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
