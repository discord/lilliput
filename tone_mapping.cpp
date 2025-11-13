#include "tone_mapping.hpp"
#include <cstring>

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
    cv::Mat* bgr_only = nullptr;
    cv::Mat* alpha_channel = nullptr;
    const cv::Mat* src_for_transform = src;

    if (has_alpha) {
        bgr_only = new cv::Mat(src->rows, src->cols, CV_8UC3);
        alpha_channel = new cv::Mat(src->rows, src->cols, CV_8UC1);

        cv::Mat channels_split[4];
        cv::split(*src, channels_split);

        cv::Mat bgr_channels[3] = {channels_split[0], channels_split[1], channels_split[2]};
        cv::merge(bgr_channels, 3, *bgr_only);
        *alpha_channel = channels_split[3];

        src_for_transform = bgr_only;
    }

    // Apply Reinhard tone mapping
    cv::Mat* dst_bgr = new cv::Mat(src_for_transform->rows, src_for_transform->cols, CV_8UC3);

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
            float luma_scaled = luma * 1.2f;  // Gentle scaling
            float luma_mapped = luma_scaled / (1.0f + luma_scaled);

            // Scale RGB channels by the luminance ratio to preserve color
            float ratio = (luma > 0.001f) ? (luma_mapped / luma) : 0.0f;

            dst_row[idx + 0] = static_cast<uint8_t>(std::min(b * ratio * 255.0f, 255.0f));
            dst_row[idx + 1] = static_cast<uint8_t>(std::min(g * ratio * 255.0f, 255.0f));
            dst_row[idx + 2] = static_cast<uint8_t>(std::min(r * ratio * 255.0f, 255.0f));
        }
    }

    // Merge alpha back if needed
    cv::Mat* result = nullptr;
    if (has_alpha) {
        result = new cv::Mat(src->rows, src->cols, src->type());
        cv::Mat bgr_channels_out[3];
        cv::split(*dst_bgr, bgr_channels_out);
        cv::Mat final_channels[4] = {bgr_channels_out[0], bgr_channels_out[1], bgr_channels_out[2], *alpha_channel};
        cv::merge(final_channels, 4, *result);
        delete bgr_only;
        delete alpha_channel;
        delete dst_bgr;
    } else {
        result = dst_bgr;
    }

    return result;
}
